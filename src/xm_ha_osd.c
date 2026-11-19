#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <time.h>

#define DVRIP_PORT 34567
#define CONFIG_PATH "/mnt/mtd/Config/ha_osd.json"
#define CONFIG_FALLBACK "/var/tmp/ha_osd.json"
#define PUSH_PATH "/var/tmp/osd_push.json"
#define RELOAD_FLAG "/var/tmp/ha_osd.reload"

#define MAX_LINES 6
#define LINE_MAX_LEN 128

typedef struct {
    int enabled;
    char ha_url[256];
    char ha_token[512];
    int poll_interval;
    int pos_x, pos_y, pos_w, pos_h;
    char lines[MAX_LINES][256];
    int line_enabled[MAX_LINES];
} ha_config_t;

static volatile int g_running = 1;
static volatile int g_reload = 1;

static void sig_handler(int sig) {
    if (sig == SIGHUP) g_reload = 1;
    else if (sig == SIGTERM || sig == SIGINT) g_running = 0;
}

// Minimal JSON string value parser
static int json_get_string(const char *json, const char *key, char *out, size_t max_len) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    char *pos = strstr(json, search);
    if (!pos) return 0;
    char *colon = strchr(pos + strlen(search), ':');
    if (!colon) return 0;
    char *start = strchr(colon, '"');
    if (!start) return 0;
    start++;
    char *end = start;
    int escape = 0;
    while (*end) {
        if (*end == '\\' && !escape) { escape = 1; end++; continue; }
        if (*end == '"' && !escape) break;
        escape = 0;
        end++;
    }
    size_t len = end - start;
    if (len >= max_len) len = max_len - 1;
    // Unescape basic json
    size_t oi = 0;
    for (size_t i = 0; i < len && oi < max_len - 1; i++) {
        if (start[i] == '\\' && i + 1 < len) {
            i++;
            if (start[i] == 'n') out[oi++] = '\n';
            else if (start[i] == 'r') out[oi++] = '\r';
            else if (start[i] == 't') out[oi++] = '\t';
            else out[oi++] = start[i];
        } else {
            out[oi++] = start[i];
        }
    }
    out[oi] = '\0';
    return 1;
}

static int json_get_int(const char *json, const char *key, int default_val) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    char *pos = strstr(json, search);
    if (!pos) return default_val;
    char *colon = strchr(pos + strlen(search), ':');
    if (!colon) return default_val;
    while (*colon == ' ' || *colon == '\t') colon++;
    return atoi(colon + 1);
}

static int json_get_bool(const char *json, const char *key, int default_val) {
    char search[128];
    snprintf(search, sizeof(search), "\"%s\"", key);
    char *pos = strstr(json, search);
    if (!pos) return default_val;
    char *colon = strchr(pos + strlen(search), ':');
    if (!colon) return default_val;
    if (strstr(colon, "true") && strstr(colon, "true") < colon + 15) return 1;
    if (strstr(colon, "false") && strstr(colon, "false") < colon + 15) return 0;
    if (strstr(colon, "1") && strstr(colon, "1") < colon + 15) return 1;
    return default_val;
}

static void load_config(ha_config_t *cfg) {
    memset(cfg, 0, sizeof(ha_config_t));
    cfg->enabled = 0;
    strncpy(cfg->ha_url, "http://10.10.11.11:8123", sizeof(cfg->ha_url));
    cfg->poll_interval = 10;
    cfg->pos_x = 160;
    cfg->pos_y = 400;
    cfg->pos_w = 4000;
    cfg->pos_h = 2000;

    FILE *f = fopen(CONFIG_PATH, "r");
    if (!f) f = fopen(CONFIG_FALLBACK, "r");
    if (!f) return;

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 16384) { fclose(f); return; }

    char *buf = malloc(sz + 1);
    if (!buf) { fclose(f); return; }
    fread(buf, 1, sz, f);
    buf[sz] = '\0';
    fclose(f);

    cfg->enabled = json_get_bool(buf, "enabled", 0);
    json_get_string(buf, "ha_url", cfg->ha_url, sizeof(cfg->ha_url));
    json_get_string(buf, "ha_token", cfg->ha_token, sizeof(cfg->ha_token));
    cfg->poll_interval = json_get_int(buf, "poll_interval", 10);
    if (cfg->poll_interval < 3) cfg->poll_interval = 3;

    // Position preset or custom
    char pos_name[32] = {0};
    if (json_get_string(buf, "osd_position", pos_name, sizeof(pos_name))) {
        if (strcmp(pos_name, "BottomLeft") == 0) {
            cfg->pos_x = 160; cfg->pos_y = 6000; cfg->pos_w = 4000; cfg->pos_h = 2000;
        } else if (strcmp(pos_name, "TopRight") == 0) {
            cfg->pos_x = 4200; cfg->pos_y = 400; cfg->pos_w = 4000; cfg->pos_h = 2000;
        } else if (strcmp(pos_name, "BottomRight") == 0) {
            cfg->pos_x = 4200; cfg->pos_y = 6000; cfg->pos_w = 4000; cfg->pos_h = 2000;
        } else { // TopLeft default
            cfg->pos_x = 160; cfg->pos_y = 400; cfg->pos_w = 4000; cfg->pos_h = 2000;
        }
    } else {
        cfg->pos_x = json_get_int(buf, "pos_x", 160);
        cfg->pos_y = json_get_int(buf, "pos_y", 400);
        cfg->pos_w = json_get_int(buf, "pos_w", 4000);
        cfg->pos_h = json_get_int(buf, "pos_h", 2000);
    }

    // Lines
    char *lines_pos = strstr(buf, "\"lines\"");
    if (lines_pos) {
        char *bracket = strchr(lines_pos, '[');
        if (bracket) {
            char *cur = bracket + 1;
            for (int i = 0; i < MAX_LINES; i++) {
                char *obj_start = strchr(cur, '{');
                char *arr_end = strchr(cur, ']');
                if (!obj_start || (arr_end && obj_start > arr_end)) break;
                char *obj_end = strchr(obj_start, '}');
                if (!obj_end) break;

                size_t olen = obj_end - obj_start + 1;
                char *obj = malloc(olen + 1);
                memcpy(obj, obj_start, olen);
                obj[olen] = '\0';

                cfg->line_enabled[i] = json_get_bool(obj, "enabled", 1);
                json_get_string(obj, "template", cfg->lines[i], sizeof(cfg->lines[i]));
                free(obj);

                cur = obj_end + 1;
            }
        }
    }

    free(buf);
}

static int recv_all(int fd, uint8_t *buf, size_t count) {
    size_t total = 0;
    while (total < count) {
        ssize_t n = recv(fd, buf + total, count - total, 0);
        if (n <= 0) return -1;
        total += n;
    }
    return 0;
}

// UTF-8 Cyrillic to Latin transliterator for clean hardware OSD rendering
static void transliterate_cyrillic(const char *src, char *dst, size_t dst_sz) {
    size_t si = 0, di = 0;
    while (src[si] && di + 4 < dst_sz) {
        unsigned char c1 = (unsigned char)src[si];
        unsigned char c2 = (unsigned char)src[si + 1];

        // Emojis / siren
        if (c1 == 0xF0 && c2 == 0x9F) {
            si += 4;
            if (di + 4 < dst_sz) { dst[di++] = '['; dst[di++] = '!'; dst[di++] = ']'; dst[di++] = ' '; }
            continue;
        }

        if (c1 == 0xD0 || c1 == 0xD1) {
            uint16_t code_pt = (c1 << 8) | c2;
            si += 2;
            const char *rep = NULL;
            switch (code_pt) {
                case 0xD090: rep = "A"; break;
                case 0xD091: rep = "B"; break;
                case 0xD092: rep = "V"; break;
                case 0xD093: rep = "G"; break;
                case 0xD094: rep = "D"; break;
                case 0xD095: rep = "E"; break;
                case 0xD081: rep = "Yo"; break;
                case 0xD096: rep = "Zh"; break;
                case 0xD097: rep = "Z"; break;
                case 0xD098: rep = "I"; break;
                case 0xD099: rep = "Y"; break;
                case 0xD09A: rep = "K"; break;
                case 0xD09B: rep = "L"; break;
                case 0xD09C: rep = "M"; break;
                case 0xD09D: rep = "N"; break;
                case 0xD09E: rep = "O"; break;
                case 0xD09F: rep = "P"; break;
                case 0xD0A0: rep = "R"; break;
                case 0xD0A1: rep = "S"; break;
                case 0xD0A2: rep = "T"; break;
                case 0xD0A3: rep = "U"; break;
                case 0xD0A4: rep = "F"; break;
                case 0xD0A5: rep = "Kh"; break;
                case 0xD0A6: rep = "Ts"; break;
                case 0xD0A7: rep = "Ch"; break;
                case 0xD0A8: rep = "Sh"; break;
                case 0xD0A9: rep = "Shch"; break;
                case 0xD0AA: rep = ""; break;
                case 0xD0AB: rep = "Y"; break;
                case 0xD0AC: rep = ""; break;
                case 0xD0AD: rep = "E"; break;
                case 0xD0AE: rep = "Yu"; break;
                case 0xD0AF: rep = "Ya"; break;

                case 0xD0B0: rep = "a"; break;
                case 0xD0B1: rep = "b"; break;
                case 0xD0B2: rep = "v"; break;
                case 0xD0B3: rep = "g"; break;
                case 0xD0B4: rep = "d"; break;
                case 0xD0B5: rep = "e"; break;
                case 0xD191: rep = "yo"; break;
                case 0xD0B6: rep = "zh"; break;
                case 0xD0B7: rep = "z"; break;
                case 0xD0B8: rep = "i"; break;
                case 0xD0B9: rep = "y"; break;
                case 0xD0BA: rep = "k"; break;
                case 0xD0BB: rep = "l"; break;
                case 0xD0BC: rep = "m"; break;
                case 0xD0BD: rep = "n"; break;
                case 0xD0BE: rep = "o"; break;
                case 0xD0BF: rep = "p"; break;
                case 0xD180: rep = "r"; break;
                case 0xD181: rep = "s"; break;
                case 0xD182: rep = "t"; break;
                case 0xD183: rep = "u"; break;
                case 0xD184: rep = "f"; break;
                case 0xD185: rep = "kh"; break;
                case 0xD186: rep = "ts"; break;
                case 0xD187: rep = "ch"; break;
                case 0xD188: rep = "sh"; break;
                case 0xD189: rep = "shch"; break;
                case 0xD18A: rep = ""; break;
                case 0xD18B: rep = "y"; break;
                case 0xD18C: rep = ""; break;
                case 0xD18D: rep = "e"; break;
                case 0xD18E: rep = "yu"; break;
                case 0xD18F: rep = "ya"; break;
                default: rep = " "; break;
            }
            if (rep) {
                while (*rep && di < dst_sz - 1) dst[di++] = *rep++;
            }
        } else {
            dst[di++] = src[si++];
        }
    }
    dst[di] = '\0';
}

// DVRIP update helper
static int dvrip_update_osd(const char rendered_lines[MAX_LINES][LINE_MAX_LEN], int px, int py, int pw, int ph) {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return -1;

    struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(DVRIP_PORT);
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        close(s);
        return -1;
    }

    // Login
    const char *login_json = "{\"EncryptType\":\"NONE\",\"LoginType\":\"DVRIP-Web\",\"PassWord\":\"\",\"UserName\":\"admin\"}\n";
    uint32_t login_len = strlen(login_json) + 1;
    uint8_t hdr[20];
    hdr[0] = 0xFF; hdr[1] = 0x01; hdr[2] = 0; hdr[3] = 0;
    uint32_t zero = 0, one = 1;
    memcpy(hdr + 4, &zero, 4);
    memcpy(hdr + 8, &one, 4);
    hdr[12] = 0; hdr[13] = 0;
    uint16_t msg_login = 1000;
    memcpy(hdr + 14, &msg_login, 2);
    memcpy(hdr + 16, &login_len, 4);

    send(s, hdr, 20, 0);
    send(s, login_json, login_len, 0);

    if (recv_all(s, hdr, 20) < 0) { close(s); return -1; }
    uint32_t resp_len = *(uint32_t *)(hdr + 16);
    char *resp_buf = malloc(resp_len + 1);
    if (!resp_buf) { close(s); return -1; }
    if (recv_all(s, (uint8_t *)resp_buf, resp_len) < 0) { free(resp_buf); close(s); return -1; }
    resp_buf[resp_len] = '\0';

    char *sess_str = strstr(resp_buf, "\"SessionID\"");
    uint32_t session_id = 0;
    if (sess_str) {
        char *colon = strchr(sess_str, ':');
        if (colon) {
            session_id = (uint32_t)strtoul(colon + 1, NULL, 0);
            if (session_id == 0) {
                char *quote = strchr(colon, '"');
                if (quote) session_id = (uint32_t)strtoul(quote + 1, NULL, 0);
            }
        }
    }
    free(resp_buf);

    if (session_id == 0) { close(s); return -1; }

    // Transliterate Cyrillic and Escape string for JSON
    char esc_lines[MAX_LINES][256];
    for (int i = 0; i < MAX_LINES; i++) {
        char tbuf[LINE_MAX_LEN * 2];
        transliterate_cyrillic(rendered_lines[i], tbuf, sizeof(tbuf));
        esc_lines[i][0] = '\0';
        size_t oi = 0;
        for (size_t si = 0; si < strlen(tbuf) && oi < sizeof(esc_lines[i]) - 2; si++) {
            char c = tbuf[si];
            if (c == '"' || c == '\\') {
                esc_lines[i][oi++] = '\\';
            }
            if (c == '\r') continue;
            if (c == '\n') c = ' ';
            esc_lines[i][oi++] = c;
        }
        esc_lines[i][oi] = '\0';
    }

    char osd_json[4096];
    snprintf(osd_json, sizeof(osd_json),
             "{\"Name\":\"fVideo.OSDInfo\",\"SessionID\":\"0x%08X\","
             "\"fVideo.OSDInfo\":{\"Align\":0,\"strEnc\":\"UTF-8\","
             "\"OSDInfo\":[{\"Info\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"],"
             "\"OSDInfoWidget\":{\"BackColor\":\"0x40000000\",\"FrontColor\":\"0xF0FFFFFF\","
             "\"EncodeBlend\":true,\"PreviewBlend\":true,\"RelativePos\":[%d,%d,%d,%d]}}]}}\n",
             session_id,
             esc_lines[0], esc_lines[1], esc_lines[2], esc_lines[3], esc_lines[4], esc_lines[5],
             px, py, pw, ph);

    uint32_t olen = strlen(osd_json) + 1;
    uint32_t two = 2;
    memcpy(hdr + 4, &session_id, 4);
    memcpy(hdr + 8, &two, 4);
    uint16_t msg_set = 1040;
    memcpy(hdr + 14, &msg_set, 2);
    memcpy(hdr + 16, &olen, 4);

    send(s, hdr, 20, 0);
    send(s, osd_json, olen, 0);

    // Read result
    if (recv_all(s, hdr, 20) == 0) {
        uint32_t rlen = *(uint32_t *)(hdr + 16);
        if (rlen > 0 && rlen < 2048) {
            char *buf = malloc(rlen + 1);
            if (buf) {
                recv_all(s, (uint8_t *)buf, rlen);
                buf[rlen] = '\0';
                free(buf);
            }
        }
    }

    close(s);
    return 0;
}

// Check push notifications override
static int check_push_override(char lines[MAX_LINES][LINE_MAX_LEN]) {
    FILE *f = fopen(PUSH_PATH, "r");
    if (!f) return 0;

    char buf[1024];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return 0;
    buf[n] = '\0';

    long expires_at = (long)json_get_int(buf, "expires_at", 0);
    time_t now = time(NULL);
    if (now > expires_at) {
        unlink(PUSH_PATH);
        return 0;
    }

    char push_text[LINE_MAX_LEN] = {0};
    int matched = 0;
    if (json_get_string(buf, "text", push_text, sizeof(push_text))) {
        int push_line = json_get_int(buf, "line", 0);
        if (push_line >= 0 && push_line < MAX_LINES) {
            strncpy(lines[push_line], push_text, LINE_MAX_LEN - 1);
            matched = 1;
        }
    }
    for (int i = 0; i < MAX_LINES; i++) {
        char key[16];
        snprintf(key, sizeof(key), "line%d", i + 1);
        char ltext[LINE_MAX_LEN] = {0};
        if (json_get_string(buf, key, ltext, sizeof(ltext)) && ltext[0]) {
            strncpy(lines[i], ltext, LINE_MAX_LEN - 1);
            matched = 1;
        }
    }
    return matched;
}

// Query Home Assistant Template API
static int query_ha_templates(const ha_config_t *cfg, char out_lines[MAX_LINES][LINE_MAX_LEN]) {
    // Check if token is provided
    if (strlen(cfg->ha_token) < 10) {
        snprintf(out_lines[0], LINE_MAX_LEN, "[HA: No Token configured]");
        return -1;
    }

    // Parse URL (e.g., http://10.10.11.11:8123 or 10.10.11.11:8123)
    char host[128] = "10.10.11.11";
    int port = 8123;
    const char *p = cfg->ha_url;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "https://", 8) == 0) p += 8;

    char host_port[160];
    strncpy(host_port, p, sizeof(host_port) - 1);
    char *slash = strchr(host_port, '/');
    if (slash) *slash = '\0';

    char *colon = strchr(host_port, ':');
    if (colon) {
        *colon = '\0';
        port = atoi(colon + 1);
    }
    strncpy(host, host_port, sizeof(host) - 1);

    // Build combined template
    // Template formats lines separated by unique marker
    char combined_tmpl[2048] = {0};
    int has_any = 0;
    for (int i = 0; i < MAX_LINES; i++) {
        if (cfg->line_enabled[i] && strlen(cfg->lines[i]) > 0) {
            has_any = 1;
            if (strlen(combined_tmpl) > 0) strncat(combined_tmpl, "###L###", sizeof(combined_tmpl) - strlen(combined_tmpl) - 1);
            strncat(combined_tmpl, cfg->lines[i], sizeof(combined_tmpl) - strlen(combined_tmpl) - 1);
        } else {
            if (strlen(combined_tmpl) > 0) strncat(combined_tmpl, "###L###", sizeof(combined_tmpl) - strlen(combined_tmpl) - 1);
        }
    }
    if (!has_any) return 0;

    // JSON escape the template string
    char esc_tmpl[4096] = {0};
    size_t ei = 0;
    for (size_t i = 0; i < strlen(combined_tmpl) && ei < sizeof(esc_tmpl) - 2; i++) {
        char c = combined_tmpl[i];
        if (c == '"' || c == '\\') esc_tmpl[ei++] = '\\';
        if (c == '\n') { esc_tmpl[ei++] = '\\'; c = 'n'; }
        if (c == '\r') continue;
        esc_tmpl[ei++] = c;
    }
    esc_tmpl[ei] = '\0';

    char req_body[4096];
    snprintf(req_body, sizeof(req_body), "{\"template\": \"%s\"}", esc_tmpl);
    int body_len = strlen(req_body);

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) {
        snprintf(out_lines[0], LINE_MAX_LEN, "[HA: Socket error]");
        return -1;
    }

    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = inet_addr(host);
    if (sin.sin_addr.s_addr == INADDR_NONE) {
        struct hostent *he = gethostbyname(host);
        if (he && he->h_addr_list[0]) {
            memcpy(&sin.sin_addr, he->h_addr_list[0], sizeof(sin.sin_addr));
        } else {
            close(s);
            snprintf(out_lines[0], LINE_MAX_LEN, "[HA: DNS resolve failed]");
            return -1;
        }
    }

    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        close(s);
        snprintf(out_lines[0], LINE_MAX_LEN, "[HA: Offline (%s)]", strerror(errno));
        return -1;
    }

    char req_hdr[1024];
    int hlen = snprintf(req_hdr, sizeof(req_hdr),
        "POST /api/template HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        host, port, cfg->ha_token, body_len);

    if (send(s, req_hdr, hlen, MSG_NOSIGNAL) < 0 ||
        send(s, req_body, body_len, MSG_NOSIGNAL) < 0) {
        close(s);
        snprintf(out_lines[0], LINE_MAX_LEN, "[HA: Send failed]");
        return -1;
    }

    // Read response
    char resp[8192];
    size_t total = 0;
    while (total < sizeof(resp) - 1) {
        ssize_t n = recv(s, resp + total, sizeof(resp) - 1 - total, 0);
        if (n <= 0) break;
        total += n;
    }
    close(s);
    resp[total] = '\0';

    if (total == 0) {
        snprintf(out_lines[0], LINE_MAX_LEN, "[HA: No response]");
        return -1;
    }

    // Parse HTTP Status
    int status_code = 0;
    if (sscanf(resp, "HTTP/1.%*d %d", &status_code) != 1) {
        snprintf(out_lines[0], LINE_MAX_LEN, "[HA: Bad HTTP response]");
        return -1;
    }

    if (status_code == 401) {
        snprintf(out_lines[0], LINE_MAX_LEN, "[HA: 401 Unauthorized - Check Token]");
        return -1;
    }
    if (status_code != 200) {
        snprintf(out_lines[0], LINE_MAX_LEN, "[HA: HTTP %d Error]", status_code);
        return -1;
    }

    char *body = strstr(resp, "\r\n\r\n");
    if (!body) {
        snprintf(out_lines[0], LINE_MAX_LEN, "[HA: Missing HTTP body]");
        return -1;
    }
    body += 4;

    // Split body by "###L###"
    char *cur = body;
    for (int i = 0; i < MAX_LINES; i++) {
        char *sep = strstr(cur, "###L###");
        if (sep) *sep = '\0';

        // Trim leading and trailing whitespace/newlines
        while (*cur == ' ' || *cur == '\t' || *cur == '\r' || *cur == '\n') cur++;
        size_t len = strlen(cur);
        while (len > 0 && (cur[len - 1] == ' ' || cur[len - 1] == '\t' || cur[len - 1] == '\r' || cur[len - 1] == '\n')) {
            cur[--len] = '\0';
        }

        if (cfg->line_enabled[i]) {
            strncpy(out_lines[i], cur, LINE_MAX_LEN - 1);
            out_lines[i][LINE_MAX_LEN - 1] = '\0';
        } else {
            out_lines[i][0] = '\0';
        }

        if (!sep) break;
        cur = sep + 7;
    }

    return 0;
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    signal(SIGPIPE, SIG_IGN);
    signal(SIGHUP, sig_handler);
    signal(SIGTERM, sig_handler);
    signal(SIGINT, sig_handler);

    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=========================================\n");
    printf(" xm_ha_osd - Home Assistant OSD Daemon\n");
    printf(" CleanCam OS GK7201V300 (Hardware OSD)\n");
    printf("=========================================\n");

    ha_config_t cfg;
    load_config(&cfg);

    char active_lines[MAX_LINES][LINE_MAX_LEN];
    for (int i = 0; i < MAX_LINES; i++) active_lines[i][0] = '\0';

    time_t last_check = 0;
    time_t cfg_mtime = 0;

    struct stat st;
    if (stat(CONFIG_PATH, &st) == 0) cfg_mtime = st.st_mtime;

    while (g_running) {
        // Check for manual reload flag or file modification
        if (access(RELOAD_FLAG, F_OK) == 0) {
            unlink(RELOAD_FLAG);
            g_reload = 1;
        } else if (stat(CONFIG_PATH, &st) == 0 && st.st_mtime != cfg_mtime) {
            cfg_mtime = st.st_mtime;
            g_reload = 1;
        }

        if (g_reload) {
            g_reload = 0;
            load_config(&cfg);
            printf("[HA-OSD] Config reloaded: enabled=%d HA=%s interval=%ds pos=[%d,%d,%d,%d]\n",
                   cfg.enabled, cfg.ha_url, cfg.poll_interval, cfg.pos_x, cfg.pos_y, cfg.pos_w, cfg.pos_h);
            last_check = 0; // Trigger immediate fetch
        }

        static int cleared = 0;
        char display_lines[MAX_LINES][LINE_MAX_LEN];
        memcpy(display_lines, active_lines, sizeof(display_lines));
        int has_push = check_push_override(display_lines);

        if (!cfg.enabled && !has_push) {
            if (!cleared) {
                char empty[MAX_LINES][LINE_MAX_LEN] = {0};
                dvrip_update_osd((const char (*)[LINE_MAX_LEN])empty, cfg.pos_x, cfg.pos_y, cfg.pos_w, cfg.pos_h);
                cleared = 1;
            }
            sleep(1);
            continue;
        }
        cleared = 0;

        if (cfg.enabled) {
            time_t now = time(NULL);
            if (now - last_check >= cfg.poll_interval) {
                last_check = now;
                char fetched_lines[MAX_LINES][LINE_MAX_LEN];
                for (int i = 0; i < MAX_LINES; i++) fetched_lines[i][0] = '\0';

                int res = query_ha_templates(&cfg, fetched_lines);
                if (res == 0) {
                    for (int i = 0; i < MAX_LINES; i++) {
                        strncpy(active_lines[i], fetched_lines[i], LINE_MAX_LEN - 1);
                    }
                } else {
                    // If error, set error line 0
                    strncpy(active_lines[0], fetched_lines[0], LINE_MAX_LEN - 1);
                }
            }
        }

        // Apply temporary PUSH alert overrides if active
        memcpy(display_lines, active_lines, sizeof(display_lines));
        check_push_override(display_lines);

        // Update DVRIP OSD
        dvrip_update_osd((const char (*)[LINE_MAX_LEN])display_lines, cfg.pos_x, cfg.pos_y, cfg.pos_w, cfg.pos_h);

        // Responsive sleep loop
        for (int i = 0; i < 2 && g_running && !g_reload; i++) {
            sleep(1);
        }
    }

    printf("[HA-OSD] Exiting...\n");
    return 0;
}

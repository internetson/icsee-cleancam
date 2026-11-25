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
#include <ctype.h>

#define DVRIP_PORT 34567
#define CONFIG_PATH "/mnt/mtd/Config/ha_osd.json"
#define CONFIG_FALLBACK "/var/tmp/ha_osd.json"
#define CAM_CFG_PATH "/mnt/mtd/Config/CameraSettings.json"
#define PUSH_PATH "/var/tmp/osd_push.json"
#define RELOAD_FLAG "/var/tmp/ha_osd.reload"

#define MAX_LINES 6
#define LINE_MAX_LEN 128

typedef struct {
    int osd_time;
    int osd_time_x;
    int osd_time_y;
    char osd_time_color[32];
    int osd_title;
    int osd_title_x;
    int osd_title_y;
    char osd_title_color[32];
    char osd_name[64];
    int osd_wifi;
} cam_osd_cfg_t;

typedef struct {
    int enabled;
    int show_wifi;
    cam_osd_cfg_t cam;
    char ha_url[256];
    char ha_token[512];
    int poll_interval;
    int round_precision;
    int pos_x, pos_y, pos_w, pos_h;
    char front_color[32];
    char back_color[32];
    int font_size;
    char lines[MAX_LINES][256];
    int line_enabled[MAX_LINES];
} ha_config_t;

typedef struct {
    int has_push;
    int px, py;
    char color[32];
    int font_size;
} push_info_t;

static void normalize_color(const char *in_col, char *out_col, size_t max_len, const char *default_col) {
    if (!in_col || !in_col[0]) {
        strncpy(out_col, default_col, max_len - 1);
        out_col[max_len - 1] = '\0';
        return;
    }
    if (strcasecmp(in_col, "white") == 0) strncpy(out_col, "0xF0FFFFFF", max_len - 1);
    else if (strcasecmp(in_col, "yellow") == 0) strncpy(out_col, "0xF0FFFF00", max_len - 1);
    else if (strcasecmp(in_col, "red") == 0) strncpy(out_col, "0xF0FF0000", max_len - 1);
    else if (strcasecmp(in_col, "green") == 0) strncpy(out_col, "0xF000FF00", max_len - 1);
    else if (strcasecmp(in_col, "cyan") == 0) strncpy(out_col, "0xF000FFFF", max_len - 1);
    else if (strcasecmp(in_col, "blue") == 0) strncpy(out_col, "0xF00000FF", max_len - 1);
    else if (strcasecmp(in_col, "magenta") == 0) strncpy(out_col, "0xF0FF00FF", max_len - 1);
    else if (strcasecmp(in_col, "orange") == 0) strncpy(out_col, "0xF0FFA500", max_len - 1);
    else if (strncasecmp(in_col, "0x", 2) == 0) {
        strncpy(out_col, in_col, max_len - 1);
    } else if (in_col[0] == '#') {
        unsigned int hex = 0;
        sscanf(in_col + 1, "%x", &hex);
        snprintf(out_col, max_len, "0xF0%06X", hex & 0xFFFFFF);
    } else {
        strncpy(out_col, in_col, max_len - 1);
    }
    out_col[max_len - 1] = '\0';
}

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
    colon++;
    while (*colon == ' ' || *colon == '\t') colon++;
    return atoi(colon);
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

static char *find_json_obj_end(const char *start) {
    if (!start || *start != '{') return NULL;
    int in_string = 0;
    int escape = 0;
    int depth = 0;
    const char *p = start;
    while (*p) {
        if (in_string) {
            if (*p == '\\' && !escape) {
                escape = 1;
            } else {
                if (*p == '"' && !escape) {
                    in_string = 0;
                }
                escape = 0;
            }
        } else {
            if (*p == '"') {
                in_string = 1;
                escape = 0;
            } else if (*p == '{') {
                depth++;
            } else if (*p == '}') {
                depth--;
                if (depth == 0) return (char *)p;
            }
        }
        p++;
    }
    return NULL;
}

static void round_floats_in_string(char *str, size_t max_len, int precision) {
    if (precision < 0 || precision > 4 || !str || max_len == 0) return;
    char out[4096];
    size_t si = 0, oi = 0;
    size_t len = strlen(str);

    while (si < len && oi + 32 < sizeof(out)) {
        char c = str[si];
        size_t num_start = si;

        if ((c >= '0' && c <= '9') || (c == '-' && si + 1 < len && str[si+1] >= '0' && str[si+1] <= '9')) {
            if (si == 0 || (!isalnum((unsigned char)str[si-1]) && str[si-1] != '.' && str[si-1] != '_')) {
                size_t p = (c == '-') ? si + 1 : si;
                while (p < len && isdigit((unsigned char)str[p])) p++;
                if (p < len && str[p] == '.') {
                    p++;
                    size_t dec_digits = 0;
                    while (p < len && isdigit((unsigned char)str[p])) {
                        p++;
                        dec_digits++;
                    }
                    if (dec_digits > 0 && (p == len || str[p] != '.')) {
                        char num_str[64];
                        size_t num_len = p - num_start;
                        if (num_len < sizeof(num_str)) {
                            memcpy(num_str, str + num_start, num_len);
                            num_str[num_len] = '\0';
                            double val = strtod(num_str, NULL);
                            char formatted[32];
                            snprintf(formatted, sizeof(formatted), "%.*f", precision, val);
                            for (size_t fi = 0; fi < strlen(formatted) && oi < sizeof(out) - 1; fi++) {
                                out[oi++] = formatted[fi];
                            }
                            si = p;
                            continue;
                        }
                    }
                }
            }
        }

        out[oi++] = str[si++];
    }
    out[oi] = '\0';
    strncpy(str, out, max_len - 1);
    str[max_len - 1] = '\0';
}


static void resolve_pos_coords(const char *pos_name, int def_x, int def_y, int *ox, int *oy) {
    if (strcmp(pos_name, "TopLeft") == 0) { *ox = 256; *oy = 512; }
    else if (strcmp(pos_name, "TopRight") == 0) { *ox = 5800; *oy = 512; }
    else if (strcmp(pos_name, "BottomLeft") == 0) { *ox = 256; *oy = 7500; }
    else if (strcmp(pos_name, "BottomRight") == 0) { *ox = 5800; *oy = 7500; }
    else { *ox = def_x; *oy = def_y; }
}

static void load_cam_osd_cfg(cam_osd_cfg_t *cam) {
    memset(cam, 0, sizeof(*cam));
    cam->osd_time = 1;
    cam->osd_time_x = 5800;
    cam->osd_time_y = 512;
    strncpy(cam->osd_time_color, "0xF0FFFFFF", sizeof(cam->osd_time_color) - 1);
    cam->osd_title = 1;
    cam->osd_title_x = 256;
    cam->osd_title_y = 512;
    strncpy(cam->osd_title_color, "0xF0FFFFFF", sizeof(cam->osd_title_color) - 1);
    strncpy(cam->osd_name, "CleanCam", sizeof(cam->osd_name) - 1);
    cam->osd_wifi = 0;

    FILE *f = fopen(CAM_CFG_PATH, "r");
    if (!f) return;
    char buf[4096];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);

    cam->osd_time = json_get_bool(buf, "osd_time", 1);
    cam->osd_title = json_get_bool(buf, "osd_title", 1);
    cam->osd_wifi = json_get_bool(buf, "osd_wifi", 0);
    json_get_string(buf, "osd_name", cam->osd_name, sizeof(cam->osd_name));

    char tpos[32] = {0}, lpos[32] = {0};
    json_get_string(buf, "osd_time_pos", tpos, sizeof(tpos));
    json_get_string(buf, "osd_title_pos", lpos, sizeof(lpos));
    resolve_pos_coords(tpos, 5800, 512, &cam->osd_time_x, &cam->osd_time_y);
    resolve_pos_coords(lpos, 256, 512, &cam->osd_title_x, &cam->osd_title_y);

    int tx = json_get_int(buf, "osd_time_x", -1);
    int ty = json_get_int(buf, "osd_time_y", -1);
    if (tx >= 0 && ty >= 0) { cam->osd_time_x = tx; cam->osd_time_y = ty; }

    int lx = json_get_int(buf, "osd_title_x", -1);
    int ly = json_get_int(buf, "osd_title_y", -1);
    if (lx >= 0 && ly >= 0) { cam->osd_title_x = lx; cam->osd_title_y = ly; }

    char raw_tc[32] = {0}, raw_lc[32] = {0};
    json_get_string(buf, "osd_time_color", raw_tc, sizeof(raw_tc));
    json_get_string(buf, "osd_title_color", raw_lc, sizeof(raw_lc));
    normalize_color(raw_tc, cam->osd_time_color, sizeof(cam->osd_time_color), "0xF0FFFFFF");
    normalize_color(raw_lc, cam->osd_title_color, sizeof(cam->osd_title_color), "0xF0FFFFFF");
}

static void get_wifi_signal_line(char *out, size_t max_len) {
    FILE *f = fopen("/proc/net/wireless", "r");
    if (!f) {
        snprintf(out, max_len, "WiFi: N/A");
        return;
    }
    char line[256];
    int line_no = 0;
    int found = 0;
    float link = 0.0f, level = -100.0f;
    while (fgets(line, sizeof(line), f)) {
        line_no++;
        if (line_no <= 2) continue;
        char *colon = strchr(line, ':');
        if (!colon) continue;
        unsigned int status = 0;
        if (sscanf(colon + 1, "%x %f %f", &status, &link, &level) >= 3) {
            found = 1;
            break;
        }
    }
    fclose(f);
    if (!found) {
        snprintf(out, max_len, "WiFi: Down");
        return;
    }
    int rssi = (int)(level > 0.0f ? (level - 256.0f) : level);
    int pct = (link > 0.0f) ? (int)((link * 100.0f) / 70.0f + 0.5f) : (2 * (rssi + 100));
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    snprintf(out, max_len, "WiFi: %d%% (%d dBm)", pct, rssi);
}

static void load_config(ha_config_t *cfg) {
    memset(cfg, 0, sizeof(ha_config_t));
    cfg->enabled = 0;
    load_cam_osd_cfg(&cfg->cam);
    cfg->show_wifi = cfg->cam.osd_wifi;
    strncpy(cfg->ha_url, "http://10.10.11.11:8123", sizeof(cfg->ha_url));
    cfg->poll_interval = 10;
    cfg->pos_x = 160;
    cfg->pos_y = 6000;
    cfg->pos_w = 4000;
    cfg->pos_h = 2000;
    strncpy(cfg->front_color, "0xF0FFFFFF", sizeof(cfg->front_color) - 1);
    strncpy(cfg->back_color, "0x40000000", sizeof(cfg->back_color) - 1);

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
    if (json_get_bool(buf, "wifi_signal", 0)) cfg->show_wifi = 1;
    json_get_string(buf, "ha_url", cfg->ha_url, sizeof(cfg->ha_url));
    json_get_string(buf, "ha_token", cfg->ha_token, sizeof(cfg->ha_token));
    cfg->poll_interval = json_get_int(buf, "poll_interval", 10);
    if (cfg->poll_interval < 3) cfg->poll_interval = 3;

    // Rounding precision (-1 = off, 0 = integer, 1 = tenth, 2 = hundredth)
    cfg->round_precision = json_get_int(buf, "round_precision", 1);

    char raw_front[32] = {0}, raw_back[32] = {0};
    json_get_string(buf, "front_color", raw_front, sizeof(raw_front));
    json_get_string(buf, "color", raw_front, sizeof(raw_front));
    normalize_color(raw_front, cfg->front_color, sizeof(cfg->front_color), "0xF0FFFFFF");

    json_get_string(buf, "back_color", raw_back, sizeof(raw_back));
    normalize_color(raw_back, cfg->back_color, sizeof(cfg->back_color), "0x40000000");

    cfg->font_size = json_get_int(buf, "font_size", 0);
    if (cfg->font_size == 0) cfg->font_size = json_get_int(buf, "size", 0);

    // Position preset or custom coordinates (dragged with mouse)
    int custom_x = json_get_int(buf, "pos_x", -1);
    int custom_y = json_get_int(buf, "pos_y", -1);
    char pos_name[32] = {0};
    json_get_string(buf, "osd_position", pos_name, sizeof(pos_name));

    if (custom_x >= 0 && custom_y >= 0 && (strcmp(pos_name, "Custom") == 0 || pos_name[0] == '\0')) {
        cfg->pos_x = custom_x;
        cfg->pos_y = custom_y;
    } else if (strcmp(pos_name, "BottomLeft") == 0) {
        cfg->pos_x = 160; cfg->pos_y = 6000;
    } else if (strcmp(pos_name, "TopRight") == 0) {
        cfg->pos_x = 4200; cfg->pos_y = 400;
    } else if (strcmp(pos_name, "BottomRight") == 0) {
        cfg->pos_x = 4200; cfg->pos_y = 6000;
    } else if (strcmp(pos_name, "Center") == 0) {
        cfg->pos_x = 2200; cfg->pos_y = 3200;
    } else if (strcmp(pos_name, "TopLeft") == 0) {
        cfg->pos_x = 160; cfg->pos_y = 400;
    } else if (custom_x >= 0 && custom_y >= 0) {
        cfg->pos_x = custom_x;
        cfg->pos_y = custom_y;
    } else {
        cfg->pos_x = 160; cfg->pos_y = 6000;
    }

    if (cfg->pos_x < 0) cfg->pos_x = 0;
    if (cfg->pos_x > 7500) cfg->pos_x = 7500;
    if (cfg->pos_y < 0) cfg->pos_y = 0;
    if (cfg->pos_y > 7500) cfg->pos_y = 7500;
    cfg->pos_w = 4000;
    cfg->pos_h = 2000;

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
                char *obj_end = find_json_obj_end(obj_start);
                if (!obj_end) break;

                size_t olen = obj_end - obj_start + 1;
                char *obj = malloc(olen + 1);
                if (!obj) break;
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

        // Emojis (4-byte UTF-8, 0xF0..0xF4)
        if (c1 >= 0xF0 && c1 <= 0xF4) {
            si += 4;
            // Skip variation selector if present (0xEF 0xB8 0x8F)
            if ((unsigned char)src[si] == 0xEF && (unsigned char)src[si + 1] == 0xB8 && (unsigned char)src[si + 2] == 0x8F) {
                si += 3;
            }
            continue;
        }

        // Degree symbol ° (0xC2 0xB0) -> replace with degree *
        if (c1 == 0xC2 && c2 == 0xB0) {
            si += 2;
            if (di < dst_sz - 1) dst[di++] = '*';
            continue;
        }

        // Other multi-byte non-Cyrillic (3-byte UTF-8, e.g. symbols)
        if (c1 >= 0xE0 && c1 <= 0xEF) {
            si += 3;
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

static int dvrip_connect_login(uint32_t *out_session_id) {
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
    if (resp_len == 0 || resp_len > 65536) { close(s); return -1; }
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
    *out_session_id = session_id;
    return s;
}

static int dvrip_send_setcfg(int s, uint32_t session_id, const char *json_str) {
    uint8_t hdr[20] = {0};
    uint32_t len = strlen(json_str) + 1;
    uint32_t two = 2;
    uint16_t msg_set = 1040;
    hdr[0] = 0xFF; hdr[1] = 0x01;
    memcpy(hdr + 4, &session_id, 4);
    memcpy(hdr + 8, &two, 4);
    memcpy(hdr + 14, &msg_set, 2);
    memcpy(hdr + 16, &len, 4);

    if (send(s, hdr, 20, 0) != 20) return -1;
    if (send(s, json_str, len, 0) != (ssize_t)len) return -1;

    if (recv_all(s, hdr, 20) == 0) {
        uint32_t rlen = *(uint32_t *)(hdr + 16);
        if (rlen > 0 && rlen <= 65536) {
            char *buf = malloc(rlen + 1);
            if (buf) {
                recv_all(s, (uint8_t *)buf, rlen);
                free(buf);
            }
        }
    }
    return 0;
}

static void escape_osd_text(const char *src, char *dst, size_t dst_sz) {
    char tbuf[LINE_MAX_LEN * 2];
    transliterate_cyrillic(src, tbuf, sizeof(tbuf));
    size_t oi = 0;
    for (size_t si = 0; tbuf[si] && oi < dst_sz - 2; si++) {
        char c = tbuf[si];
        if (c == '"' || c == '\\') dst[oi++] = '\\';
        if (c == '\r') continue;
        if (c == '\n') c = ' ';
        dst[oi++] = c;
    }
    dst[oi] = '\0';
}

// Update AVEnc.VideoWidget (ChannelTitle + TimeTitle) so Wi-Fi signal never collides with HA OSD or clock
static int dvrip_update_channel_title(const cam_osd_cfg_t *cam, const char *raw_title, int encode_blend) {
    uint32_t session_id = 0;
    int s = dvrip_connect_login(&session_id);
    if (s < 0) return -1;

    char esc_title[256];
    escape_osd_text(raw_title, esc_title, sizeof(esc_title));

    char json_buf[1024];
    snprintf(json_buf, sizeof(json_buf),
             "{\"Name\":\"AVEnc.VideoWidget\",\"SessionID\":\"0x%08X\","
             "\"AVEnc.VideoWidget\":[{\"ChannelTitle\":{\"Name\":\"%s\"},"
             "\"ChannelTitleAttribute\":{\"EncodeBlend\":%s,\"FrontColor\":\"%s\",\"RelativePos\":[%d,%d,255,24]},"
             "\"TimeTitleAttribute\":{\"EncodeBlend\":%s,\"FrontColor\":\"%s\",\"RelativePos\":[%d,%d,255,24]}}]}\\n",
             session_id,
             esc_title,
             encode_blend ? "true" : "false", cam->osd_title_color, cam->osd_title_x, cam->osd_title_y,
             cam->osd_time ? "true" : "false", cam->osd_time_color, cam->osd_time_x, cam->osd_time_y);

    int rc = dvrip_send_setcfg(s, session_id, json_buf);
    close(s);
    return rc;
}

// DVRIP update helper for HA OSD / Push (fVideo.OSDInfo)
static int dvrip_update_osd(const char rendered_lines[MAX_LINES][LINE_MAX_LEN], int px, int py, int pw, int ph, const char *front_color, const char *back_color, int font_size) {
    uint32_t session_id = 0;
    int s = dvrip_connect_login(&session_id);
    if (s < 0) return -1;
    uint8_t hdr[20] = {0};
    hdr[0] = 0xFF; hdr[1] = 0x01;

    // Transliterate Cyrillic and Escape string for JSON
    // Compact non-empty active lines so Sofia renders all rows consecutively
    char esc_lines[MAX_LINES][256];
    for (int i = 0; i < MAX_LINES; i++) esc_lines[i][0] = '\0';

    int active_cnt = 0;
    for (int i = 0; i < MAX_LINES; i++) {
        if (!rendered_lines[i][0]) continue;
        char tbuf[LINE_MAX_LEN * 2];
        transliterate_cyrillic(rendered_lines[i], tbuf, sizeof(tbuf));
        size_t oi = 0;
        for (size_t si = 0; si < strlen(tbuf) && oi < sizeof(esc_lines[active_cnt]) - 2; si++) {
            char c = tbuf[si];
            if (c == '"' || c == '\\') {
                esc_lines[active_cnt][oi++] = '\\';
            }
            if (c == '\r') continue;
            if (c == '\n') c = ' ';
            esc_lines[active_cnt][oi++] = c;
        }
        esc_lines[active_cnt][oi] = '\0';
        if (oi > 0) active_cnt++;
    }

    if (font_size == 16 || font_size == 24 || font_size == 32 || font_size == 48) {
        char font_json[256];
        snprintf(font_json, sizeof(font_json),
                 "{\"Name\":\"General.General\",\"SessionID\":\"0x%08X\","
                 "\"General.General\":{\"FontSize\":%d}}\n", session_id, font_size);
        uint32_t flen = strlen(font_json) + 1;
        uint8_t fhdr[20];
        uint32_t ftwo = 2;
        uint16_t fmsg_set = 1040;
        fhdr[0] = 0xFF; fhdr[1] = 0x01; fhdr[2] = 0; fhdr[3] = 0;
        memcpy(fhdr + 4, &session_id, 4);
        memcpy(fhdr + 8, &ftwo, 4);
        fhdr[12] = 0; fhdr[13] = 0;
        memcpy(fhdr + 14, &fmsg_set, 2);
        memcpy(fhdr + 16, &flen, 4);
        send(s, fhdr, 20, 0);
        send(s, font_json, flen, 0);
        if (recv_all(s, fhdr, 20) == 0) {
            uint32_t frlen = *(uint32_t *)(fhdr + 16);
            if (frlen > 0 && frlen <= 65536) {
                char *fbuf = malloc(frlen);
                if (fbuf) { recv_all(s, (uint8_t*)fbuf, frlen); free(fbuf); }
            }
        }
    }

    int eff_ph = ph;
    int needed_h = (active_cnt > 0 ? active_cnt : 1) * 450 + 200;
    if (eff_ph < needed_h) eff_ph = needed_h;

    char osd_json[4096];
    snprintf(osd_json, sizeof(osd_json),
             "{\"Name\":\"fVideo.OSDInfo\",\"SessionID\":\"0x%08X\","
             "\"fVideo.OSDInfo\":{\"Align\":0,\"strEnc\":\"UTF-8\",\"osdInfoNums\":%d,"
             "\"OSDInfo\":[{\"Info\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"],"
             "\"OSDInfoWidget\":{\"BackColor\":\"%s\",\"FrontColor\":\"%s\","
             "\"EncodeBlend\":true,\"PreviewBlend\":true,\"RelativePos\":[%d,%d,%d,%d]}}]}}\n",
             session_id,
             active_cnt > 0 ? active_cnt : 1,
             esc_lines[0], esc_lines[1], esc_lines[2], esc_lines[3], esc_lines[4], esc_lines[5],
             (back_color && back_color[0]) ? back_color : "0x40000000",
             (front_color && front_color[0]) ? front_color : "0xF0FFFFFF",
             px, py, pw, eff_ph);

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
static int check_push_override(char lines[MAX_LINES][LINE_MAX_LEN], push_info_t *pinfo) {
    if (pinfo) {
        pinfo->has_push = 0;
        pinfo->px = -1;
        pinfo->py = -1;
        pinfo->color[0] = '\0';
        pinfo->font_size = 0;
    }
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

    if (matched && pinfo) {
        pinfo->has_push = 1;
        int kx = json_get_int(buf, "x", -1);
        if (kx < 0) kx = json_get_int(buf, "pos_x", -1);
        int ky = json_get_int(buf, "y", -1);
        if (ky < 0) ky = json_get_int(buf, "pos_y", -1);
        pinfo->px = kx;
        pinfo->py = ky;
        char raw_col[32] = {0};
        if (json_get_string(buf, "color", raw_col, sizeof(raw_col)) || json_get_string(buf, "front_color", raw_col, sizeof(raw_col))) {
            normalize_color(raw_col, pinfo->color, sizeof(pinfo->color), "0xF0FFFFFF");
        }
        int sz = json_get_int(buf, "size", 0);
        if (sz == 0) sz = json_get_int(buf, "font_size", 0);
        pinfo->font_size = sz;
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

    int last_idx = -1;
    for (int i = MAX_LINES - 1; i >= 0; i--) {
        if (cfg->line_enabled[i] && strlen(cfg->lines[i]) > 0) {
            last_idx = i;
            break;
        }
    }
    if (last_idx < 0) return 0;

    char combined_tmpl[2048] = {0};
    for (int i = 0; i <= last_idx; i++) {
        if (i > 0) strncat(combined_tmpl, "###L###", sizeof(combined_tmpl) - strlen(combined_tmpl) - 1);
        if (cfg->line_enabled[i] && strlen(cfg->lines[i]) > 0) {
            strncat(combined_tmpl, cfg->lines[i], sizeof(combined_tmpl) - strlen(combined_tmpl) - 1);
        }
    }

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
        "POST /api/template HTTP/1.0\r\n"
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

    // Split body by "###L###" safely without modifying pointers in .rodata
    char *cur = body;
    for (int i = 0; i < MAX_LINES; i++) {
        out_lines[i][0] = '\0';
        if (i <= last_idx && cur && *cur) {
            char *sep = strstr(cur, "###L###");
            if (sep) *sep = '\0';

            char line_buf[LINE_MAX_LEN] = {0};
            strncpy(line_buf, cur, sizeof(line_buf) - 1);

            // Trim leading whitespace
            char *s = line_buf;
            while (*s == ' ' || *s == '\t' || *s == '\r' || *s == '\n') s++;
            if (s != line_buf) {
                memmove(line_buf, s, strlen(s) + 1);
            }

            // Trim trailing whitespace
            size_t len = strlen(line_buf);
            while (len > 0 && (line_buf[len - 1] == ' ' || line_buf[len - 1] == '\t' || line_buf[len - 1] == '\r' || line_buf[len - 1] == '\n')) {
                line_buf[--len] = '\0';
            }

            if (cfg->line_enabled[i]) {
                if (cfg->round_precision >= 0) {
                    round_floats_in_string(line_buf, sizeof(line_buf), cfg->round_precision);
                }
                strncpy(out_lines[i], line_buf, LINE_MAX_LEN - 1);
                out_lines[i][LINE_MAX_LEN - 1] = '\0';
            }

            if (sep) cur = sep + 7;
            else cur = NULL;
        }
    }

    return 0;
}

static int run_test_mode(const char *input) {
    char ha_url[256] = "http://10.10.11.11:8123";
    char ha_token[512] = {0};
    char tmpl[1024] = "HA Connection OK | Time: {{ now().strftime('%H:%M:%S') }}";

    char *buf = NULL;
    FILE *f = fopen(input, "r");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz > 0 && sz < 32768) {
            buf = malloc(sz + 1);
            if (buf) {
                fread(buf, 1, sz, f);
                buf[sz] = '\0';
            }
        }
        fclose(f);
    }
    if (!buf) {
        buf = strdup(input);
    }

    int test_round = 1;
    if (buf) {
        json_get_string(buf, "ha_url", ha_url, sizeof(ha_url));
        json_get_string(buf, "ha_token", ha_token, sizeof(ha_token));
        json_get_string(buf, "test_template", tmpl, sizeof(tmpl));
        if (strlen(tmpl) == 0) {
            json_get_string(buf, "template", tmpl, sizeof(tmpl));
        }
        test_round = json_get_int(buf, "round_precision", 1);
        free(buf);
        buf = NULL;
    }

    if (strlen(ha_token) < 5) {
        printf("{\"status\": \"error\", \"message\": \"Укажите токен доступа Home Assistant\"}\n");
        return 1;
    }
    if (strlen(tmpl) == 0) {
        strncpy(tmpl, "HA Connection OK | Time: {{ now().strftime('%H:%M:%S') }}", sizeof(tmpl) - 1);
    }

    // Parse URL
    char host[128] = "10.10.11.11";
    int port = 8123;
    const char *p = ha_url;
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

    // Escape template
    char esc_tmpl[2048] = {0};
    size_t ei = 0;
    for (size_t i = 0; i < strlen(tmpl) && ei < sizeof(esc_tmpl) - 2; i++) {
        char c = tmpl[i];
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
        printf("{\"status\": \"error\", \"message\": \"Ошибка создания сокета на камере\"}\n");
        return 1;
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
            printf("{\"status\": \"error\", \"message\": \"Не удалось разрешить DNS имя сервера Home Assistant\"}\n");
            return 1;
        }
    }

    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        close(s);
        printf("{\"status\": \"error\", \"message\": \"Не удалось подключиться к Home Assistant (%s)\"}\n", strerror(errno));
        return 1;
    }

    char req_hdr[1024];
    int hlen = snprintf(req_hdr, sizeof(req_hdr),
        "POST /api/template HTTP/1.0\r\n"
        "Host: %s:%d\r\n"
        "Authorization: Bearer %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        host, port, ha_token, body_len);

    if (send(s, req_hdr, hlen, MSG_NOSIGNAL) < 0 ||
        send(s, req_body, body_len, MSG_NOSIGNAL) < 0) {
        close(s);
        printf("{\"status\": \"error\", \"message\": \"Ошибка отправки запроса в Home Assistant\"}\n");
        return 1;
    }

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
        printf("{\"status\": \"error\", \"message\": \"Home Assistant не ответил на запрос\"}\n");
        return 1;
    }

    int status_code = 0;
    if (sscanf(resp, "HTTP/1.%*d %d", &status_code) != 1) {
        printf("{\"status\": \"error\", \"message\": \"Некорректный HTTP ответ от Home Assistant\"}\n");
        return 1;
    }

    if (status_code == 401) {
        printf("{\"status\": \"error\", \"message\": \"Ошибка 401: Неверный токен доступа Home Assistant\"}\n");
        return 1;
    }
    if (status_code != 200) {
        printf("{\"status\": \"error\", \"message\": \"Home Assistant вернул HTTP ошибку %d\"}\n", status_code);
        return 1;
    }

    char *body = strstr(resp, "\r\n\r\n");
    if (!body) body = strstr(resp, "\n\n");
    if (!body) {
        printf("{\"status\": \"error\", \"message\": \"Отсутствует тело ответа от Home Assistant\"}\n");
        return 1;
    }
    body = (body[0] == '\r') ? body + 4 : body + 2;

    while (*body == '\r' || *body == '\n') body++;
    size_t blen = strlen(body);
    while (blen > 0 && (body[blen - 1] == '\r' || body[blen - 1] == '\n')) {
        body[--blen] = '\0';
    }

    if (test_round >= 0) {
        size_t rem_sz = sizeof(resp) - (size_t)(body - resp);
        round_floats_in_string(body, rem_sz, test_round);
        blen = strlen(body);
    }

    char esc_out[4096] = {0};
    size_t oi = 0;
    for (size_t i = 0; i < blen && oi < sizeof(esc_out) - 2; i++) {
        char c = body[i];
        if (c == '"' || c == '\\') esc_out[oi++] = '\\';
        if (c == '\r') continue;
        if (c == '\n') { esc_out[oi++] = '\\'; c = 'n'; }
        esc_out[oi++] = c;
    }
    esc_out[oi] = '\0';

    printf("{\"status\": \"ok\", \"message\": \"Связь с Home Assistant успешно установлена!\", \"rendered\": \"%s\"}\n", esc_out);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "--test") == 0) {
        const char *input = (argc >= 3) ? argv[2] : "/var/tmp/ha_test.json";
        return run_test_mode(input);
    }

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
    time_t cam_mtime = 0;

    struct stat st;
    if (stat(CONFIG_PATH, &st) == 0) cfg_mtime = st.st_mtime;
    if (stat(CAM_CFG_PATH, &st) == 0) cam_mtime = st.st_mtime;

    while (g_running) {
        // Check for manual reload flag or file modification
        if (access(RELOAD_FLAG, F_OK) == 0) {
            unlink(RELOAD_FLAG);
            g_reload = 1;
        } else if (stat(CONFIG_PATH, &st) == 0 && st.st_mtime != cfg_mtime) {
            cfg_mtime = st.st_mtime;
            g_reload = 1;
        } else if (stat(CAM_CFG_PATH, &st) == 0 && st.st_mtime != cam_mtime) {
            cam_mtime = st.st_mtime;
            g_reload = 1;
        }

        static char last_sent_lines[MAX_LINES][LINE_MAX_LEN] = {{0}};
        static int last_x = -1, last_y = -1, last_sz = -1;
        static char last_col[32] = {0}, last_bcol[32] = {0};
        static time_t last_send_time = 0;
        static char last_wifi_title[128] = {0};
        static time_t last_wifi_poll = 0;
        static time_t last_wifi_send = 0;
        static int wifi_was_active = 0;

        if (g_reload) {
            g_reload = 0;
            load_config(&cfg);
            if (!cfg.enabled) {
                for (int i = 0; i < MAX_LINES; i++) active_lines[i][0] = '\0';
            }
            printf("[HA-OSD] Config reloaded: enabled=%d wifi=%d HA=%s interval=%ds pos=[%d,%d,%d,%d]\n",
                   cfg.enabled, cfg.show_wifi, cfg.ha_url, cfg.poll_interval, cfg.pos_x, cfg.pos_y, cfg.pos_w, cfg.pos_h);
            last_check = 0; // Trigger immediate fetch
            last_send_time = 0;
            last_wifi_poll = 0;
            last_wifi_send = 0;
        }

        // Handle Wi-Fi signal display in AVEnc.VideoWidget.ChannelTitle independently of HA OSD
        time_t now_loop = time(NULL);
        if (cfg.show_wifi) {
            if (now_loop - last_wifi_poll >= 4) {
                last_wifi_poll = now_loop;
                char wifi_str[64];
                get_wifi_signal_line(wifi_str, sizeof(wifi_str));
                char desired_title[128];
                if (cfg.cam.osd_title && cfg.cam.osd_name[0]) {
                    snprintf(desired_title, sizeof(desired_title), "%s | %s", cfg.cam.osd_name, wifi_str);
                } else {
                    snprintf(desired_title, sizeof(desired_title), "%s", wifi_str);
                }
                if (last_wifi_send == 0 || (now_loop - last_wifi_send >= 30) || strcmp(desired_title, last_wifi_title) != 0) {
                    if (dvrip_update_channel_title(&cfg.cam, desired_title, 1) == 0) {
                        strncpy(last_wifi_title, desired_title, sizeof(last_wifi_title) - 1);
                        last_wifi_title[sizeof(last_wifi_title) - 1] = '\0';
                        last_wifi_send = now_loop;
                        wifi_was_active = 1;
                    }
                }
            }
        } else if (wifi_was_active) {
            if (dvrip_update_channel_title(&cfg.cam, cfg.cam.osd_name, cfg.cam.osd_title) == 0) {
                last_wifi_title[0] = '\0';
                last_wifi_send = 0;
                wifi_was_active = 0;
            }
        }

        static int cleared = 0;
        char display_lines[MAX_LINES][LINE_MAX_LEN];
        memcpy(display_lines, active_lines, sizeof(display_lines));
        push_info_t pinfo;
        int has_push = check_push_override(display_lines, &pinfo);

        if (!cfg.enabled && !has_push) {
            if (!cleared) {
                char empty[MAX_LINES][LINE_MAX_LEN] = {{0}};
                dvrip_update_osd((const char (*)[LINE_MAX_LEN])empty, cfg.pos_x, cfg.pos_y, cfg.pos_w, cfg.pos_h, cfg.front_color, cfg.back_color, 0);
                memset(last_sent_lines, 0, sizeof(last_sent_lines));
                last_send_time = 0;
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
                        active_lines[i][LINE_MAX_LEN - 1] = '\0';
                    }
                } else {
                    // If error, set error line 0 and clear stale lines
                    strncpy(active_lines[0], fetched_lines[0], LINE_MAX_LEN - 1);
                    active_lines[0][LINE_MAX_LEN - 1] = '\0';
                    for (int i = 1; i < MAX_LINES; i++) active_lines[i][0] = '\0';
                }
            }
        }

        // Apply temporary PUSH alert overrides if active
        memcpy(display_lines, active_lines, sizeof(display_lines));
        check_push_override(display_lines, &pinfo);

        int cur_x = cfg.pos_x;
        int cur_y = cfg.pos_y;
        const char *cur_col = cfg.front_color;
        int cur_sz = cfg.font_size;
        if (pinfo.has_push) {
            if (pinfo.px >= 0) cur_x = pinfo.px;
            if (pinfo.py >= 0) cur_y = pinfo.py;
            if (pinfo.color[0]) cur_col = pinfo.color;
            if (pinfo.font_size > 0) cur_sz = pinfo.font_size;
        }

        time_t cur_t = time(NULL);
        int changed = (last_send_time == 0) || (cur_t - last_send_time >= 30) ||
                      (cur_x != last_x) || (cur_y != last_y) || (cur_sz != last_sz) ||
                      (strcmp(cur_col, last_col) != 0) || (strcmp(cfg.back_color, last_bcol) != 0);
        if (!changed) {
            for (int i = 0; i < MAX_LINES; i++) {
                if (strcmp(display_lines[i], last_sent_lines[i]) != 0) {
                    changed = 1;
                    break;
                }
            }
        }
        if (changed) {
            if (dvrip_update_osd((const char (*)[LINE_MAX_LEN])display_lines, cur_x, cur_y, cfg.pos_w, cfg.pos_h, cur_col, cfg.back_color, cur_sz) == 0) {
                memcpy(last_sent_lines, display_lines, sizeof(last_sent_lines));
                last_x = cur_x; last_y = cur_y; last_sz = cur_sz;
                strncpy(last_col, cur_col, sizeof(last_col) - 1);
                strncpy(last_bcol, cfg.back_color, sizeof(last_bcol) - 1);
                last_send_time = cur_t;
            }
        }

        // Responsive sleep loop
        for (int i = 0; i < 2 && g_running && !g_reload; i++) {
            sleep(1);
        }
    }

    printf("[HA-OSD] Exiting...\n");
    return 0;
}

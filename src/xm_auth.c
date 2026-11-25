#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/select.h>

#define AUTH_FILE "/mnt/mtd/Config/web_auth.json"
#define TOKEN_FILE "/var/tmp/web_session.token"
#define STREAM_AUTH_FILE "/mnt/mtd/Config/stream_auth.json"
#define DEFAULT_USER "admin"
#define DEFAULT_PASS "admin"
#define DEFAULT_STREAM_USER "stream"
#define DEFAULT_STREAM_PASS "stream123"

static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit((unsigned char)a) && isxdigit((unsigned char)b))) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = (char)(16 * a + b);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst++ = '\0';
}

static void json_escape(const char *src, char *dst, size_t max_len) {
    size_t j = 0;
    if (!src || max_len == 0) {
        if (max_len > 0) dst[0] = '\0';
        return;
    }
    for (size_t i = 0; src[i] && j + 2 < max_len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') {
            dst[j++] = '\\';
            dst[j++] = (char)c;
        } else if (c == '\n') {
            dst[j++] = '\\';
            dst[j++] = 'n';
        } else if (c == '\r') {
            dst[j++] = '\\';
            dst[j++] = 'r';
        } else if (c == '\t') {
            dst[j++] = '\\';
            dst[j++] = 't';
        } else if (c >= 0x20) {
            dst[j++] = (char)c;
        }
    }
    dst[j] = '\0';
}

static void extract_json_str(const char *buf, const char *key, char *out, size_t max_len) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(buf, needle);
    if (!p) return;
    const char *col = strchr(p + strlen(needle), ':');
    if (!col) return;
    const char *q1 = strchr(col, '"');
    if (!q1) return;
    q1++;
    size_t oi = 0;
    int esc = 0;
    while (*q1 && oi < max_len - 1) {
        if (*q1 == '\\' && !esc) {
            esc = 1;
            q1++;
            continue;
        }
        if (*q1 == '"' && !esc) break;
        if (esc) {
            if (*q1 == 'n') out[oi++] = '\n';
            else if (*q1 == 'r') out[oi++] = '\r';
            else if (*q1 == 't') out[oi++] = '\t';
            else out[oi++] = *q1;
            esc = 0;
        } else {
            out[oi++] = *q1;
        }
        q1++;
    }
    out[oi] = '\0';
}

static int b64_char_to_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static int base64_decode(const char *in, char *out, size_t max_out) {
    if (!in || !out || max_out == 0) return 0;
    size_t in_len = strlen(in);
    size_t out_idx = 0;
    for (size_t i = 0; i < in_len; ) {
        while (i < in_len && (in[i] == ' ' || in[i] == '\r' || in[i] == '\n' || in[i] == '\t')) i++;
        if (i >= in_len) break;
        int a = b64_char_to_val(in[i++]);
        int b = (i < in_len) ? b64_char_to_val(in[i++]) : -1;
        int c = (i < in_len) ? b64_char_to_val(in[i++]) : -1;
        int d = (i < in_len) ? b64_char_to_val(in[i++]) : -1;
        if (a < 0 || b < 0) break;
        if (out_idx < max_out - 1) out[out_idx++] = (char)((a << 2) | (b >> 4));
        if (c >= 0 && out_idx < max_out - 1) out[out_idx++] = (char)(((b & 0xF) << 4) | (c >> 2));
        if (d >= 0 && out_idx < max_out - 1) out[out_idx++] = (char)(((c & 0x3) << 6) | d);
    }
    out[out_idx] = '\0';
    return (int)out_idx;
}

static void get_query_param(const char *query, const char *key, char *out, size_t max_len) {
    out[0] = '\0';
    if (!query || strlen(query) == 0) return;
    size_t klen = strlen(key);
    const char *p = query;
    while (p && *p) {
        if ((p == query || *(p - 1) == '&') && strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *val = p + klen + 1;
            const char *amp = strchr(val, '&');
            size_t vlen = amp ? (size_t)(amp - val) : strlen(val);
            char *tmp = malloc(vlen + 1);
            if (tmp) {
                memcpy(tmp, val, vlen);
                tmp[vlen] = '\0';
                char *dec = malloc(vlen + 1);
                if (dec) {
                    url_decode(dec, tmp);
                    strncpy(out, dec, max_len - 1);
                    out[max_len - 1] = '\0';
                    free(dec);
                }
                free(tmp);
            }
            return;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
}

static void get_json_param(const char *json, const char *key, char *out, size_t max_len) {
    out[0] = '\0';
    if (!json) return;
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return;
    p += strlen(needle);
    while (*p && (*p == ' ' || *p == '\t' || *p == ':')) p++;
    if (*p == '"') {
        extract_json_str(json, key, out, max_len);
    } else {
        size_t len = 0;
        while (p[len] && p[len] != ',' && p[len] != '}' && p[len] != ' ' && p[len] != '\r' && p[len] != '\n') {
            len++;
        }
        if (len >= max_len) len = max_len - 1;
        strncpy(out, p, len);
        out[len] = '\0';
    }
}

static void get_param(const char *query, const char *body, const char *key, char *out, size_t max_len) {
    out[0] = '\0';
    if (query && query[0]) get_query_param(query, key, out, max_len);
    if (out[0] != '\0') return;
    if (body && body[0]) {
        if (strchr(body, '{')) {
            get_json_param(body, key, out, max_len);
            if (out[0] != '\0') return;
        }
        get_query_param(body, key, out, max_len);
    }
}

static void load_credentials(char *user, size_t ulen, char *pass, size_t plen) {
    strncpy(user, DEFAULT_USER, ulen - 1);
    user[ulen - 1] = '\0';
    strncpy(pass, DEFAULT_PASS, plen - 1);
    pass[plen - 1] = '\0';

    FILE *f = fopen(AUTH_FILE, "r");
    if (!f) return;

    char buf[1024] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    extract_json_str(buf, "username", user, ulen);
    extract_json_str(buf, "password", pass, plen);
}

static int save_credentials(const char *user, const char *pass) {
    char esc_u[256], esc_p[256];
    json_escape(user, esc_u, sizeof(esc_u));
    json_escape(pass, esc_p, sizeof(esc_p));

    FILE *f = fopen(AUTH_FILE, "w");
    if (!f) return -1;
    fprintf(f, "{\n  \"username\": \"%s\",\n  \"password\": \"%s\"\n}\n", esc_u, esc_p);
    fclose(f);
    sync();
    return 0;
}

static void load_stream_credentials(int *enabled, char *user, size_t ulen, char *pass, size_t plen) {
    *enabled = 0;
    strncpy(user, DEFAULT_STREAM_USER, ulen - 1);
    user[ulen - 1] = '\0';
    strncpy(pass, DEFAULT_STREAM_PASS, plen - 1);
    pass[plen - 1] = '\0';

    FILE *f = fopen(STREAM_AUTH_FILE, "r");
    if (!f) return;

    char buf[1024] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    char *en = strstr(buf, "\"enabled\"");
    if (en) {
        char *col = strchr(en, ':');
        if (col) {
            col++;
            while (*col == ' ' || *col == '\t') col++;
            if (*col == '1' || strncmp(col, "true", 4) == 0) *enabled = 1;
            else *enabled = 0;
        }
    }

    extract_json_str(buf, "username", user, ulen);
    extract_json_str(buf, "password", pass, plen);
}

static int save_stream_credentials(int enabled, const char *user, const char *pass) {
    char esc_u[256], esc_p[256];
    json_escape(user, esc_u, sizeof(esc_u));
    json_escape(pass, esc_p, sizeof(esc_p));

    FILE *f = fopen(STREAM_AUTH_FILE, "w");
    if (!f) return -1;
    fprintf(f, "{\n  \"enabled\": %s,\n  \"username\": \"%s\",\n  \"password\": \"%s\"\n}\n",
            enabled ? "true" : "false", esc_u, esc_p);
    fclose(f);
    sync();
    return 0;
}

static void generate_token(char *token, size_t max_len) {
    unsigned int r1 = (unsigned int)time(NULL);
    unsigned int r2 = (unsigned int)getpid();
    int fd = open("/dev/urandom", O_RDONLY);
    unsigned int r3 = 0, r4 = 0;
    if (fd >= 0) {
        read(fd, &r3, sizeof(r3));
        read(fd, &r4, sizeof(r4));
        close(fd);
    }
    snprintf(token, max_len, "cc_%08x%08x%08x", r1 ^ r4, r2, r3);

    FILE *f = fopen(TOKEN_FILE, "w");
    if (f) {
        fprintf(f, "%s\n", token);
        fclose(f);
    }
}

static int is_valid_token(const char *token) {
    if (!token || strlen(token) < 8) return 0;

    FILE *f = fopen(TOKEN_FILE, "r");
    if (!f) return 0;

    char saved[128] = {0};
    if (fgets(saved, sizeof(saved), f)) {
        char *nl = strchr(saved, '\r');
        if (nl) *nl = '\0';
        nl = strchr(saved, '\n');
        if (nl) *nl = '\0';
    }
    fclose(f);

    if (saved[0] == '\0') return 0;
    return (strcmp(token, saved) == 0);
}

static int verify_admin_env(void) {
    const char *qs = getenv("QUERY_STRING");
    const char *auth_hdr = getenv("HTTP_AUTHORIZATION");

    char token[128] = {0};
    if (qs) get_query_param(qs, "token", token, sizeof(token));
    if (token[0] == '\0' && auth_hdr && strncasecmp(auth_hdr, "Bearer ", 7) == 0) {
        const char *b = auth_hdr + 7;
        while (*b == ' ') b++;
        strncpy(token, b, sizeof(token) - 1);
    }
    if (is_valid_token(token)) return 0;

    if (auth_hdr && strncasecmp(auth_hdr, "Basic ", 6) == 0) {
        const char *b64 = auth_hdr + 6;
        while (*b64 == ' ') b64++;
        char dec[256] = {0};
        base64_decode(b64, dec, sizeof(dec));
        char *col = strchr(dec, ':');
        if (col) {
            *col = '\0';
            char cur_user[64], cur_pass[64];
            load_credentials(cur_user, sizeof(cur_user), cur_pass, sizeof(cur_pass));
            if (strcmp(dec, cur_user) == 0 && strcmp(col + 1, cur_pass) == 0) {
                return 0;
            }
        }
    }
    return 1;
}

static int verify_stream_env(void) {
    int stream_en = 0;
    char stream_user[64], stream_pass[64];
    load_stream_credentials(&stream_en, stream_user, sizeof(stream_user), stream_pass, sizeof(stream_pass));
    if (!stream_en) return 0;

    if (verify_admin_env() == 0) return 0;

    char adm_user[64], adm_pass[64];
    load_credentials(adm_user, sizeof(adm_user), adm_pass, sizeof(adm_pass));

    const char *qs = getenv("QUERY_STRING");
    if (qs) {
        char u[64] = {0}, p[64] = {0};
        get_query_param(qs, "user", u, sizeof(u));
        get_query_param(qs, "pass", p, sizeof(p));
        if (u[0] && p[0]) {
            if (strcmp(u, stream_user) == 0 && strcmp(p, stream_pass) == 0) return 0;
            if (strcmp(u, adm_user) == 0 && strcmp(p, adm_pass) == 0) return 0;
        }
    }

    const char *auth_hdr = getenv("HTTP_AUTHORIZATION");
    if (auth_hdr && strncasecmp(auth_hdr, "Basic ", 6) == 0) {
        const char *b64 = auth_hdr + 6;
        while (*b64 == ' ') b64++;
        char dec[256] = {0};
        base64_decode(b64, dec, sizeof(dec));
        char *col = strchr(dec, ':');
        if (col) {
            *col = '\0';
            if (strcmp(dec, stream_user) == 0 && strcmp(col + 1, stream_pass) == 0) return 0;
            if (strcmp(dec, adm_user) == 0 && strcmp(col + 1, adm_pass) == 0) return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc >= 2) {
        if (strcmp(argv[1], "--verify-admin") == 0) {
            return verify_admin_env();
        }
        if (strcmp(argv[1], "--verify-stream") == 0) {
            return verify_stream_env();
        }
    }

    printf("Content-Type: application/json; charset=utf-8\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Cache-Control: no-cache, no-store\r\n\r\n");

    const char *qs = getenv("QUERY_STRING");
    char post_data[2048] = {0};

    const char *method = getenv("REQUEST_METHOD");
    if (method && strcmp(method, "POST") == 0) {
        const char *cl_env = getenv("CONTENT_LENGTH");
        size_t to_read = cl_env ? (size_t)atoi(cl_env) : (sizeof(post_data) - 1);
        if (to_read >= sizeof(post_data)) to_read = sizeof(post_data) - 1;
        size_t total = 0;
        while (total < to_read) {
            fd_set fds;
            struct timeval tv = {0, 100000};
            FD_ZERO(&fds);
            FD_SET(0, &fds);
            if (select(1, &fds, NULL, NULL, &tv) <= 0) break;
            ssize_t n = read(0, post_data + total, to_read - total);
            if (n <= 0) break;
            total += (size_t)n;
        }
        post_data[total] = '\0';
    }

    char action[32] = {0};
    char token[128] = {0};
    char user[64] = {0};
    char pass[64] = {0};
    char old_pass[64] = {0};
    char new_pass[64] = {0};
    char enabled_str[16] = {0};
    char stream_user[64] = {0};
    char stream_pass[64] = {0};

    get_param(qs, post_data, "action", action, sizeof(action));
    get_param(qs, post_data, "token", token, sizeof(token));
    get_param(qs, post_data, "user", user, sizeof(user));
    get_param(qs, post_data, "pass", pass, sizeof(pass));
    get_param(qs, post_data, "old_pass", old_pass, sizeof(old_pass));
    get_param(qs, post_data, "new_pass", new_pass, sizeof(new_pass));
    get_param(qs, post_data, "enabled", enabled_str, sizeof(enabled_str));

    get_param(qs, post_data, "stream_user", stream_user, sizeof(stream_user));
    if (stream_user[0] == '\0') get_param(qs, post_data, "username", stream_user, sizeof(stream_user));

    get_param(qs, post_data, "stream_pass", stream_pass, sizeof(stream_pass));
    if (stream_pass[0] == '\0') get_param(qs, post_data, "password", stream_pass, sizeof(stream_pass));

    char cur_user[64] = {0}, cur_pass[64] = {0};
    load_credentials(cur_user, sizeof(cur_user), cur_pass, sizeof(cur_pass));

    int cur_stream_en = 0;
    char cur_stream_user[64] = {0}, cur_stream_pass[64] = {0};
    load_stream_credentials(&cur_stream_en, cur_stream_user, sizeof(cur_stream_user), cur_stream_pass, sizeof(cur_stream_pass));

    char esc_u[256], esc_su[256], esc_sp[256];
    json_escape(cur_user, esc_u, sizeof(esc_u));
    json_escape(cur_stream_user, esc_su, sizeof(esc_su));
    json_escape(cur_stream_pass, esc_sp, sizeof(esc_sp));

    if (strcmp(action, "check") == 0) {
        if (is_valid_token(token)) {
            printf("{\"status\":\"ok\",\"authenticated\":true,\"username\":\"%s\"}\n", esc_u);
        } else {
            printf("{\"status\":\"ok\",\"authenticated\":false}\n");
        }
    } else if (strcmp(action, "login") == 0) {
        if (strlen(user) == 0) {
            printf("{\"status\":\"error\",\"message\":\"Введите имя пользователя\"}\n");
        } else if ((strlen(cur_stream_user) > 0 && strcmp(user, cur_stream_user) == 0) ||
                   strcmp(user, DEFAULT_STREAM_USER) == 0) {
            // STRICT SECURITY: Stream user is NEVER allowed to login to WebUI!
            printf("{\"status\":\"error\",\"message\":\"Пользователь потоков предназначен только для RTSP и API и не имеет доступа к веб-интерфейсу\"}\n");
        } else if (strcmp(user, cur_user) == 0 && strcmp(pass, cur_pass) == 0) {
            char new_tok[128];
            generate_token(new_tok, sizeof(new_tok));
            printf("{\"status\":\"ok\",\"token\":\"%s\",\"username\":\"%s\"}\n", new_tok, esc_u);
        } else {
            printf("{\"status\":\"error\",\"message\":\"Неверный логин или пароль\"}\n");
        }
    } else if (strcmp(action, "change_password") == 0) {
        if (!is_valid_token(token)) {
            printf("{\"status\":\"error\",\"message\":\"Сессия истекла. Войдите снова.\"}\n");
            return 0;
        }
        if (strcmp(old_pass, cur_pass) != 0) {
            printf("{\"status\":\"error\",\"message\":\"Неверный текущий пароль!\"}\n");
            return 0;
        }
        if (strlen(new_pass) < 1) {
            printf("{\"status\":\"error\",\"message\":\"Новый пароль не может быть пустым!\"}\n");
            return 0;
        }
        if (save_credentials(cur_user, new_pass) < 0) {
            printf("{\"status\":\"error\",\"message\":\"Ошибка записи пароля в память!\"}\n");
            return 0;
        }
        char new_tok[128];
        generate_token(new_tok, sizeof(new_tok));
        printf("{\"status\":\"ok\",\"message\":\"Пароль успешно обновлен!\",\"token\":\"%s\"}\n", new_tok);
    } else if (strcmp(action, "get_stream_auth") == 0) {
        if (!is_valid_token(token)) {
            printf("{\"status\":\"error\",\"message\":\"Сессия истекла. Войдите снова.\"}\n");
            return 0;
        }
        printf("{\"status\":\"ok\",\"enabled\":%s,\"username\":\"%s\",\"password\":\"%s\"}\n",
               cur_stream_en ? "true" : "false", esc_su, esc_sp);
    } else if (strcmp(action, "set_stream_auth") == 0) {
        if (!is_valid_token(token)) {
            printf("{\"status\":\"error\",\"message\":\"Сессия истекла. Войдите снова.\"}\n");
            return 0;
        }
        int new_en = (strcmp(enabled_str, "1") == 0 || strcasecmp(enabled_str, "true") == 0);
        if (strlen(stream_user) < 1) {
            printf("{\"status\":\"error\",\"message\":\"Логин пользователя потоков не может быть пустым!\"}\n");
            return 0;
        }
        if (strcmp(stream_user, cur_user) == 0) {
            printf("{\"status\":\"error\",\"message\":\"Имя пользователя потоков не должно совпадать с логином WebUI!\"}\n");
            return 0;
        }
        if (strlen(stream_pass) < 1) {
            printf("{\"status\":\"error\",\"message\":\"Пароль пользователя потоков не может быть пустым!\"}\n");
            return 0;
        }
        if (save_stream_credentials(new_en, stream_user, stream_pass) < 0) {
            printf("{\"status\":\"error\",\"message\":\"Ошибка сохранения настроек безопасности потоков!\"}\n");
            return 0;
        }
        printf("{\"status\":\"ok\",\"message\":\"Настройки безопасности потоков успешно сохранены!\"}\n");
    } else if (strcmp(action, "logout") == 0) {
        if (!is_valid_token(token)) {
            printf("{\"status\":\"error\",\"message\":\"Unauthorized\"}\n");
            return 0;
        }
        unlink(TOKEN_FILE);
        printf("{\"status\":\"ok\",\"message\":\"Сессия завершена\"}\n");
    } else {
        printf("{\"status\":\"error\",\"message\":\"Неизвестное действие\"}\n");
    }

    return 0;
}

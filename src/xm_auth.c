#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/select.h>

#define AUTH_FILE "/mnt/mtd/Config/web_auth.json"
#define TOKEN_FILE "/var/tmp/web_session.token"
#define DEFAULT_USER "admin"
#define DEFAULT_PASS "admin"

// URL decode helper
static void url_decode(char *dst, const char *src) {
    char a, b;
    while (*src) {
        if ((*src == '%') &&
            ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a'-'A';
            if (a >= 'A') a -= ('A' - 10);
            else a -= '0';
            if (b >= 'a') b -= 'a'-'A';
            if (b >= 'A') b -= ('A' - 10);
            else b -= '0';
            *dst++ = 16*a + b;
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
                strncpy(tmp, val, vlen);
                tmp[vlen] = '\0';
                url_decode(out, tmp);
                free(tmp);
            }
            return;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
}

static void get_param(const char *query, const char *body, const char *key, char *out, size_t max_len) {
    out[0] = '\0';
    if (query) get_query_param(query, key, out, max_len);
    if (out[0] != '\0') return;
    if (body) get_query_param(body, key, out, max_len);
}

static void load_credentials(char *user, size_t ulen, char *pass, size_t plen) {
    strncpy(user, DEFAULT_USER, ulen);
    strncpy(pass, DEFAULT_PASS, plen);

    FILE *f = fopen(AUTH_FILE, "r");
    if (!f) return;

    char buf[1024] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    char *u = strstr(buf, "\"username\"");
    if (u) {
        char *col = strchr(u, ':');
        if (col) {
            char *q1 = strchr(col, '"');
            if (q1) {
                char *q2 = strchr(q1 + 1, '"');
                if (q2) {
                    size_t l = q2 - (q1 + 1);
                    if (l >= ulen) l = ulen - 1;
                    strncpy(user, q1 + 1, l);
                    user[l] = '\0';
                }
            }
        }
    }

    char *p = strstr(buf, "\"password\"");
    if (p) {
        char *col = strchr(p, ':');
        if (col) {
            char *q1 = strchr(col, '"');
            if (q1) {
                char *q2 = strchr(q1 + 1, '"');
                if (q2) {
                    size_t l = q2 - (q1 + 1);
                    if (l >= plen) l = plen - 1;
                    strncpy(pass, q1 + 1, l);
                    pass[l] = '\0';
                }
            }
        }
    }
}

static int save_credentials(const char *user, const char *pass) {
    system("mkdir -p /mnt/mtd/Config");
    FILE *f = fopen(AUTH_FILE, "w");
    if (!f) return -1;
    fprintf(f, "{\n  \"username\": \"%s\",\n  \"password\": \"%s\"\n}\n", user, pass);
    fclose(f);
    system("sync");
    return 0;
}

static int is_valid_token(const char *token) {
    if (!token || strlen(token) < 16) return 0;
    FILE *f = fopen(TOKEN_FILE, "r");
    if (!f) return 0;
    char saved[128] = {0};
    if (fscanf(f, "%127s", saved) != 1) { fclose(f); return 0; }
    fclose(f);
    return (strcmp(token, saved) == 0);
}

static void generate_token(char *out, size_t max_len) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    snprintf(out, max_len, "cc_%lx_%lx_%08x", (unsigned long)tv.tv_sec, (unsigned long)tv.tv_usec, (unsigned int)rand());
    FILE *f = fopen(TOKEN_FILE, "w");
    if (f) {
        fprintf(f, "%s\n", out);
        fclose(f);
    }
}

int main(int argc, char **argv) {
    printf("Content-Type: application/json\r\n"
           "Access-Control-Allow-Origin: *\r\n"
           "Cache-Control: no-cache, no-store\r\n\r\n");

    const char *qs = getenv("QUERY_STRING");
    char post_data[2048] = {0};

    // Non-blocking read of stdin in case of POST body
    const char *method = getenv("REQUEST_METHOD");
    if (method && strcmp(method, "POST") == 0) {
        fd_set fds;
        struct timeval tv = {0, 20000}; // 20ms max timeout
        FD_ZERO(&fds);
        FD_SET(0, &fds);
        if (select(1, &fds, NULL, NULL, &tv) > 0) {
            int flags = fcntl(0, F_GETFL, 0);
            fcntl(0, F_SETFL, flags | O_NONBLOCK);
            ssize_t n = read(0, post_data, sizeof(post_data) - 1);
            if (n > 0) {
                post_data[n] = '\0';
            }
        }
    }

    char action[32] = {0};
    char token[128] = {0};
    char user[64] = {0};
    char pass[64] = {0};
    char old_pass[64] = {0};
    char new_pass[64] = {0};

    get_param(qs, post_data, "action", action, sizeof(action));
    get_param(qs, post_data, "token", token, sizeof(token));
    get_param(qs, post_data, "user", user, sizeof(user));
    get_param(qs, post_data, "pass", pass, sizeof(pass));
    get_param(qs, post_data, "old_pass", old_pass, sizeof(old_pass));
    get_param(qs, post_data, "new_pass", new_pass, sizeof(new_pass));

    char cur_user[64] = {0}, cur_pass[64] = {0};
    load_credentials(cur_user, sizeof(cur_user), cur_pass, sizeof(cur_pass));

    if (strcmp(action, "check") == 0) {
        if (is_valid_token(token)) {
            printf("{\"status\":\"ok\",\"authenticated\":true,\"username\":\"%s\"}\n", cur_user);
        } else {
            printf("{\"status\":\"ok\",\"authenticated\":false}\n");
        }
    } else if (strcmp(action, "login") == 0) {
        if (strlen(user) == 0 && strlen(pass) == 0) {
            printf("{\"status\":\"error\",\"message\":\"Введите логин и пароль\"}\n");
        } else if (strcmp(user, cur_user) == 0 && strcmp(pass, cur_pass) == 0) {
            char new_tok[128];
            generate_token(new_tok, sizeof(new_tok));
            printf("{\"status\":\"ok\",\"token\":\"%s\",\"username\":\"%s\"}\n", new_tok, cur_user);
        } else {
            printf("{\"status\":\"error\",\"message\":\"Неверный логин или пароль\"}\n");
        }
    } else if (strcmp(action, "change_password") == 0) {
        if (!is_valid_token(token)) {
            printf("{\"status\":\"error\",\"message\":\"Сессия недействительна. Войдите снова.\"}\n");
            return 0;
        }
        if (strcmp(old_pass, cur_pass) != 0) {
            printf("{\"status\":\"error\",\"message\":\"Текущий пароль указан неверно!\"}\n");
            return 0;
        }
        if (strlen(new_pass) < 1) {
            printf("{\"status\":\"error\",\"message\":\"Новый пароль не может быть пустым!\"}\n");
            return 0;
        }
        if (save_credentials(cur_user, new_pass) < 0) {
            printf("{\"status\":\"error\",\"message\":\"Ошибка записи конфигурации во флеш!\"}\n");
            return 0;
        }
        char new_tok[128];
        generate_token(new_tok, sizeof(new_tok));
        printf("{\"status\":\"ok\",\"message\":\"Пароль успешно изменен!\",\"token\":\"%s\"}\n", new_tok);
    } else if (strcmp(action, "logout") == 0) {
        unlink(TOKEN_FILE);
        printf("{\"status\":\"ok\"}\n");
    } else {
        printf("{\"status\":\"error\",\"message\":\"Неизвестное действие\"}\n");
    }

    return 0;
}

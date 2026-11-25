#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#define DVRIP_PORT 34567
#define SNAP_CACHE "/var/tmp/snapshot.jpg"

static int recv_all(int fd, uint8_t *buf, size_t count) {
    size_t total = 0;
    while (total < count) {
        ssize_t n = recv(fd, buf + total, count - total, 0);
        if (n <= 0) return -1;
        total += n;
    }
    return 0;
}

static long get_file_mtime_ms(const char *path) {
    struct stat st;
    if (stat(path, &st) < 0) return 0;
    return st.st_mtime * 1000L;
}

static long get_current_time_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000L + tv.tv_usec / 1000L;
}

static int fetch_dvrip_snapshot(uint8_t **out_buf, size_t *out_len) {
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

    // 1. Login (1000)
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

    if (send(s, hdr, 20, 0) < 0 || send(s, login_json, login_len, 0) < 0) {
        close(s);
        return -1;
    }

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

    // 2. OPSNAP (1560)
    char snap_json[128];
    snprintf(snap_json, sizeof(snap_json),
             "{\"Name\":\"OPSNAP\",\"SessionID\":\"0x%08X\",\"OPSNAP\":{\"Channel\":0}}\n",
             session_id);
    uint32_t snap_len = strlen(snap_json) + 1;
    hdr[0] = 0xFF; hdr[1] = 0x01; hdr[2] = 0; hdr[3] = 0;
    memcpy(hdr + 4, &session_id, 4);
    uint32_t two = 2;
    memcpy(hdr + 8, &two, 4);
    hdr[12] = 0; hdr[13] = 0;
    uint16_t msg_snap = 1560;
    memcpy(hdr + 14, &msg_snap, 2);
    memcpy(hdr + 16, &snap_len, 4);

    if (send(s, hdr, 20, 0) < 0 || send(s, snap_json, snap_len, 0) < 0) {
        close(s);
        return -1;
    }

    if (recv_all(s, hdr, 20) < 0) { close(s); return -1; }
    uint32_t img_len = *(uint32_t *)(hdr + 16);
    if (img_len == 0 || img_len > 2 * 1024 * 1024) { close(s); return -1; }

    uint8_t *img_buf = malloc(img_len);
    if (!img_buf) { close(s); return -1; }
    if (recv_all(s, img_buf, img_len) < 0) {
        free(img_buf);
        close(s);
        return -1;
    }

    close(s);

    *out_buf = img_buf;
    *out_len = img_len;

    // Cache to RAM file
    int cfd = open(SNAP_CACHE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (cfd >= 0) {
        write(cfd, img_buf, img_len);
        close(cfd);
    }

    return 0;
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

static int check_snapshot_auth(void) {
    FILE *f = fopen("/mnt/mtd/Config/stream_auth.json", "r");
    if (!f) return 1; // No stream auth config -> allow

    char buf[1024] = {0};
    fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);

    int enabled = 0;
    char *en = strstr(buf, "\"enabled\"");
    if (en) {
        char *col = strchr(en, ':');
        if (col) {
            while (*col == ':' || *col == ' ' || *col == '\t') col++;
            if (*col == '1' || strncmp(col, "true", 4) == 0) enabled = 1;
        }
    }
    if (!enabled) return 1; // Stream auth disabled -> allow

    char stream_user[64] = "stream", stream_pass[64] = "stream123";
    char *u = strstr(buf, "\"username\"");
    if (u) {
        char *q1 = strchr(u, ':');
        if (q1) {
            q1 = strchr(q1, '"');
            if (q1) {
                char *q2 = strchr(q1 + 1, '"');
                if (q2) {
                    size_t l = q2 - (q1 + 1);
                    if (l >= sizeof(stream_user)) l = sizeof(stream_user) - 1;
                    strncpy(stream_user, q1 + 1, l);
                    stream_user[l] = '\0';
                }
            }
        }
    }
    char *p = strstr(buf, "\"password\"");
    if (p) {
        char *q1 = strchr(p, ':');
        if (q1) {
            q1 = strchr(q1, '"');
            if (q1) {
                char *q2 = strchr(q1 + 1, '"');
                if (q2) {
                    size_t l = q2 - (q1 + 1);
                    if (l >= sizeof(stream_pass)) l = sizeof(stream_pass) - 1;
                    strncpy(stream_pass, q1 + 1, l);
                    stream_pass[l] = '\0';
                }
            }
        }
    }

    // Load admin credentials fallback
    char admin_user[64] = "admin", admin_pass[64] = "admin";
    FILE *fa = fopen("/mnt/mtd/Config/web_auth.json", "r");
    if (fa) {
        char abuf[1024] = {0};
        fread(abuf, 1, sizeof(abuf) - 1, fa);
        fclose(fa);
        char *au = strstr(abuf, "\"username\"");
        if (au) {
            char *q1 = strchr(au, ':');
            if (q1) {
                q1 = strchr(q1, '"');
                if (q1) {
                    char *q2 = strchr(q1 + 1, '"');
                    if (q2) {
                        size_t l = q2 - (q1 + 1);
                        if (l >= sizeof(admin_user)) l = sizeof(admin_user) - 1;
                        strncpy(admin_user, q1 + 1, l);
                        admin_user[l] = '\0';
                    }
                }
            }
        }
        char *ap = strstr(abuf, "\"password\"");
        if (ap) {
            char *q1 = strchr(ap, ':');
            if (q1) {
                q1 = strchr(q1, '"');
                if (q1) {
                    char *q2 = strchr(q1 + 1, '"');
                    if (q2) {
                        size_t l = q2 - (q1 + 1);
                        if (l >= sizeof(admin_pass)) l = sizeof(admin_pass) - 1;
                        strncpy(admin_pass, q1 + 1, l);
                        admin_pass[l] = '\0';
                    }
                }
            }
        }
    }

    const char *qs = getenv("QUERY_STRING");

    // 1. Check token in QUERY_STRING (used by authenticated WebUI sessions)
    if (qs) {
        const char *t = strstr(qs, "token=");
        if (t) {
            t += 6;
            char req_tok[128] = {0};
            size_t ti = 0;
            while (*t && *t != '&' && ti < sizeof(req_tok) - 1) {
                req_tok[ti++] = *t++;
            }
            req_tok[ti] = '\0';

            FILE *tf = fopen("/var/tmp/web_session.token", "r");
            if (tf) {
                char saved_tok[128] = {0};
                if (fscanf(tf, "%127s", saved_tok) == 1 && strcmp(req_tok, saved_tok) == 0) {
                    fclose(tf);
                    return 1;
                }
                fclose(tf);
            }
        }
    }

    // 2. Check HTTP_AUTHORIZATION header (Basic <base64>)
    const char *auth_hdr = getenv("HTTP_AUTHORIZATION");
    if (auth_hdr && strncasecmp(auth_hdr, "Basic ", 6) == 0) {
        const char *b64 = auth_hdr + 6;
        while (*b64 == ' ') b64++;
        char decoded[128] = {0};
        base64_decode(b64, decoded, sizeof(decoded));
        char *colon = strchr(decoded, ':');
        if (colon) {
            *colon = '\0';
            const char *u_in = decoded;
            const char *p_in = colon + 1;
            if (strcmp(u_in, stream_user) == 0 && strcmp(p_in, stream_pass) == 0) return 1;
            if (strcmp(u_in, admin_user) == 0 && strcmp(p_in, admin_pass) == 0) return 1;
        }
    }

    // 3. Check user & pass in QUERY_STRING (convenient for curl / Home Assistant / scripts)
    if (qs) {
        const char *u_ptr = strstr(qs, "user=");
        const char *p_ptr = strstr(qs, "pass=");
        if (u_ptr && p_ptr) {
            char u_val[64] = {0}, p_val[64] = {0};
            u_ptr += 5;
            size_t ui = 0;
            while (*u_ptr && *u_ptr != '&' && ui < sizeof(u_val) - 1) u_val[ui++] = *u_ptr++;
            u_val[ui] = '\0';

            p_ptr += 5;
            size_t pi = 0;
            while (*p_ptr && *p_ptr != '&' && pi < sizeof(p_val) - 1) p_val[pi++] = *p_ptr++;
            p_val[pi] = '\0';

            if (strcmp(u_val, stream_user) == 0 && strcmp(p_val, stream_pass) == 0) return 1;
            if (strcmp(u_val, admin_user) == 0 && strcmp(p_val, admin_pass) == 0) return 1;
        }
    }

    return 0; // Unauthorized
}

int main(int argc, char **argv) {
    int is_cgi = (getenv("REQUEST_METHOD") != NULL);
    if (is_cgi && !check_snapshot_auth()) {
        printf("Status: 401 Unauthorized\r\n"
               "WWW-Authenticate: Basic realm=\"CleanCam\"\r\n"
               "Content-Type: text/plain\r\n\r\n"
               "401 Unauthorized\n");
        return 0;
    }
    long now = get_current_time_ms();
    long cache_mtime = get_file_mtime_ms(SNAP_CACHE);

    uint8_t *img_data = NULL;
    size_t img_len = 0;

    // If cache is fresh (< 400ms old) and file exists, read from cache
    if (cache_mtime > 0 && (now - cache_mtime) < 400) {
        int fd = open(SNAP_CACHE, O_RDONLY);
        if (fd >= 0) {
            struct stat st;
            if (fstat(fd, &st) == 0 && st.st_size > 0 && st.st_size <= 2 * 1024 * 1024) {
                img_len = (size_t)st.st_size;
                img_data = malloc(img_len);
                if (img_data && read(fd, img_data, img_len) == (ssize_t)img_len) {
                    close(fd);
                    goto serve;
                }
                free(img_data);
                img_data = NULL;
            }
            close(fd);
        }
    }

    // Fetch fresh from DVRIP
    if (fetch_dvrip_snapshot(&img_data, &img_len) < 0) {
        // Fallback: try cache even if older
        int fd = open(SNAP_CACHE, O_RDONLY);
        int ok = 0;
        if (fd >= 0) {
            struct stat st;
            if (fstat(fd, &st) == 0 && st.st_size > 0 && st.st_size <= 2 * 1024 * 1024) {
                img_len = (size_t)st.st_size;
                img_data = malloc(img_len);
                if (img_data && read(fd, img_data, img_len) == (ssize_t)img_len) {
                    ok = 1;
                } else {
                    free(img_data);
                    img_data = NULL;
                }
            }
            close(fd);
        }
        if (!ok) {
            if (is_cgi) {
                printf("Status: 503 Service Unavailable\r\nContent-Type: text/plain\r\n\r\nFailed to capture snapshot.\n");
            } else {
                fprintf(stderr, "Failed to capture snapshot.\n");
            }
            return 1;
        }
    }

serve:
    if (is_cgi) {
        printf("Content-Type: image/jpeg\r\n"
               "Content-Length: %zu\r\n"
               "Cache-Control: no-cache, no-store, must-revalidate\r\n"
               "Pragma: no-cache\r\n"
               "Expires: 0\r\n\r\n", img_len);
        fflush(stdout);
        write(1, img_data, img_len);
    } else if (argc > 1) {
        FILE *f = fopen(argv[1], "wb");
        if (f) {
            fwrite(img_data, 1, img_len, f);
            fclose(f);
            printf("Saved snapshot (%zu bytes) to %s\n", img_len, argv[1]);
        }
    } else {
        write(1, img_data, img_len);
    }

    free(img_data);
    return 0;
}

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
    char *resp_buf = malloc(resp_len + 1);
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

int main(int argc, char **argv) {
    int is_cgi = (getenv("REQUEST_METHOD") != NULL);
    long now = get_current_time_ms();
    long cache_mtime = get_file_mtime_ms(SNAP_CACHE);

    uint8_t *img_data = NULL;
    size_t img_len = 0;

    // If cache is fresh (< 400ms old) and file exists, read from cache
    if (cache_mtime > 0 && (now - cache_mtime) < 400) {
        int fd = open(SNAP_CACHE, O_RDONLY);
        if (fd >= 0) {
            struct stat st;
            fstat(fd, &st);
            img_len = st.st_size;
            img_data = malloc(img_len);
            if (read(fd, img_data, img_len) == (ssize_t)img_len) {
                close(fd);
                goto serve;
            }
            close(fd);
            free(img_data);
            img_data = NULL;
        }
    }

    // Fetch fresh from DVRIP
    if (fetch_dvrip_snapshot(&img_data, &img_len) < 0) {
        // Fallback: try cache even if older
        int fd = open(SNAP_CACHE, O_RDONLY);
        if (fd >= 0) {
            struct stat st;
            fstat(fd, &st);
            img_len = st.st_size;
            img_data = malloc(img_len);
            read(fd, img_data, img_len);
            close(fd);
        } else {
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

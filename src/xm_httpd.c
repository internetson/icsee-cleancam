#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define WEB_ROOT "/mnt/web"
#define DEFAULT_PORT 8080
#define BUFFER_SIZE 8192
#define MAX_CONTENT_LEN (1024 * 1024)
#define MAX_CHILDREN 24

static int is_safe_url_path(const char *url_path) {
    if (!url_path || url_path[0] != '/') return 0;
    if (strstr(url_path, "..") != NULL) return 0;
    if (strcasestr(url_path, "%2e") != NULL) return 0;
    for (const char *p = url_path; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == '\\') return 0;
    }
    return 1;
}

static int is_safe_cgi_name(const char *name) {
    if (!name || !name[0]) return 0;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return 0;
    for (const char *p = name; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) return 0;
    }
    return 1;
}

static const char *get_mime_type(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcasecmp(ext, ".html") == 0 || strcasecmp(ext, ".htm") == 0) return "text/html; charset=utf-8";
    if (strcasecmp(ext, ".js") == 0) return "application/javascript; charset=utf-8";
    if (strcasecmp(ext, ".css") == 0) return "text/css; charset=utf-8";
    if (strcasecmp(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (strcasecmp(ext, ".png") == 0) return "image/png";
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcasecmp(ext, ".svg") == 0) return "image/svg+xml";
    if (strcasecmp(ext, ".ico") == 0) return "image/x-icon";
    if (strcasecmp(ext, ".ttf") == 0) return "font/ttf";
    if (strcasecmp(ext, ".woff") == 0) return "font/woff";
    if (strcasecmp(ext, ".woff2") == 0) return "font/woff2";
    if (strcasecmp(ext, ".txt") == 0) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

static void send_cors_options(int fd) {
    const char *resp =
        "HTTP/1.0 200 OK\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n";
    send(fd, resp, strlen(resp), 0);
}

static void send_error_resp(int fd, const char *status, const char *body) {
    char resp[512];
    int len = snprintf(resp, sizeof(resp),
        "HTTP/1.0 %s\r\n"
        "Content-Type: text/plain; charset=utf-8\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n"
        "%s\r\n", status, body);
    send(fd, resp, len, 0);
}

static void send_404(int fd) {
    send_error_resp(fd, "404 Not Found", "404 Not Found");
}

static void handle_static(int fd, const char *url_path) {
    if (!is_safe_url_path(url_path)) {
        send_error_resp(fd, "403 Forbidden", "403 Forbidden");
        return;
    }

    char filepath[512];
    if (strcmp(url_path, "/") == 0) {
        snprintf(filepath, sizeof(filepath), "%s/index.html", WEB_ROOT);
    } else {
        snprintf(filepath, sizeof(filepath), "%s%s", WEB_ROOT, url_path);
    }

    char resolved[PATH_MAX];
    if (!realpath(filepath, resolved)) {
        send_404(fd);
        return;
    }
    size_t root_len = strlen(WEB_ROOT);
    if (strncmp(resolved, WEB_ROOT, root_len) != 0 ||
        (resolved[root_len] != '/' && resolved[root_len] != '\0')) {
        send_error_resp(fd, "403 Forbidden", "403 Forbidden");
        return;
    }

    int f = open(resolved, O_RDONLY);
    if (f < 0) {
        send_404(fd);
        return;
    }

    struct stat st;
    if (fstat(f, &st) < 0 || !S_ISREG(st.st_mode)) {
        close(f);
        send_404(fd);
        return;
    }

    char hdr[512];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Connection: close\r\n\r\n",
        get_mime_type(resolved), (long)st.st_size);

    send(fd, hdr, hlen, 0);

    char buf[4096];
    ssize_t n;
    while ((n = read(f, buf, sizeof(buf))) > 0) {
        if (send(fd, buf, n, 0) < 0) break;
    }
    close(f);
}

static void handle_cgi(int fd, const char *method, const char *script_url, const char *query_string,
                       const char *content_type, int content_length,
                       const char *auth_header,
                       const char *body_chunk, int body_chunk_len,
                       const char *client_ip) {
    if (!is_safe_url_path(script_url) || strncmp(script_url, "/cgi-bin/", 9) != 0) {
        send_error_resp(fd, "403 Forbidden", "403 Forbidden");
        return;
    }
    const char *name = script_url + 9;
    if (!is_safe_cgi_name(name)) {
        send_error_resp(fd, "403 Forbidden", "403 Forbidden");
        return;
    }

    char script_path[512];
    snprintf(script_path, sizeof(script_path), "%s/cgi-bin/%s", WEB_ROOT, name);

    // If script doesn't exist in /mnt/web, check /var/tmp
    if (access(script_path, X_OK) != 0) {
        snprintf(script_path, sizeof(script_path), "/var/tmp/%s", name);
        if (access(script_path, X_OK) != 0) {
            send_404(fd);
            return;
        }
    }

    int in_pipe[2];
    int out_pipe[2];
    if (pipe(in_pipe) < 0) {
        send_error_resp(fd, "500 Internal Server Error", "Pipe error");
        return;
    }
    if (pipe(out_pipe) < 0) {
        close(in_pipe[0]);
        close(in_pipe[1]);
        send_error_resp(fd, "500 Internal Server Error", "Pipe error");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(in_pipe[0]); close(in_pipe[1]);
        close(out_pipe[0]); close(out_pipe[1]);
        send_error_resp(fd, "503 Service Unavailable", "Fork failed");
        return;
    }

    if (pid == 0) {
        // Child: CGI process
        close(in_pipe[1]);
        dup2(in_pipe[0], STDIN_FILENO);
        close(in_pipe[0]);

        close(out_pipe[0]);
        dup2(out_pipe[1], STDOUT_FILENO);
        close(out_pipe[1]);
        close(fd);

        char env_method[64];
        char env_query[4096];
        char env_cl[64];
        char env_ct[256];
        char env_auth[512];
        char env_ip[64];
        char env_script[512];

        snprintf(env_method, sizeof(env_method), "REQUEST_METHOD=%s", method);
        snprintf(env_query, sizeof(env_query), "QUERY_STRING=%s", query_string ? query_string : "");
        snprintf(env_cl, sizeof(env_cl), "CONTENT_LENGTH=%d", content_length);
        snprintf(env_ct, sizeof(env_ct), "CONTENT_TYPE=%s", content_type ? content_type : "");
        snprintf(env_auth, sizeof(env_auth), "HTTP_AUTHORIZATION=%s", auth_header ? auth_header : "");
        snprintf(env_ip, sizeof(env_ip), "REMOTE_ADDR=%s", client_ip ? client_ip : "");
        snprintf(env_script, sizeof(env_script), "SCRIPT_FILENAME=%s", script_path);

        char *envp[] = {
            env_method,
            env_query,
            env_cl,
            env_ct,
            env_auth,
            env_ip,
            env_script,
            "PATH=/bin:/sbin:/usr/bin:/usr/sbin:/mnt/custom:/var/tmp",
            "LD_LIBRARY_PATH=/mnt/web/lib:/usr/local/lib:/usr/lib",
            NULL
        };

        char *argv[] = { script_path, NULL };
        execve(script_path, argv, envp);
        _exit(127);
    }

    // Parent: pipe body to child stdin ONLY if content_length > 0
    close(in_pipe[0]);
    close(out_pipe[1]);

    if (content_length > 0 && body_chunk_len > 0) {
        int to_write = body_chunk_len;
        if (to_write > content_length) to_write = content_length;
        int written = 0;
        while (written < to_write) {
            ssize_t w = write(in_pipe[1], body_chunk + written, to_write - written);
            if (w <= 0) break;
            written += (int)w;
        }
        content_length -= to_write;
    }

    // Read remaining body from socket if any
    char buf[4096];
    while (content_length > 0) {
        int to_read = content_length > (int)sizeof(buf) ? (int)sizeof(buf) : content_length;
        ssize_t n = read(fd, buf, to_read);
        if (n <= 0) break;
        ssize_t written = 0;
        while (written < n) {
            ssize_t w = write(in_pipe[1], buf + written, n - written);
            if (w <= 0) break;
            written += w;
        }
        content_length -= (int)n;
    }
    close(in_pipe[1]); // Close write end so child receives EOF

    // Read first chunk from child stdout to parse RFC 3875 Status header
    ssize_t first_n = read(out_pipe[0], buf, sizeof(buf));
    if (first_n > 0) {
        const char *data_start = buf;
        size_t data_len = first_n;

        if (strncmp(buf, "Status: ", 8) == 0 || strncmp(buf, "status: ", 8) == 0) {
            char *eol = strstr(buf, "\r\n");
            int eol_len = 2;
            if (!eol) { eol = strchr(buf, '\n'); eol_len = 1; }
            if (eol) {
                char status_num[64] = {0};
                size_t slen = eol - (buf + 8);
                if (slen >= sizeof(status_num)) slen = sizeof(status_num) - 1;
                strncpy(status_num, buf + 8, slen);
                status_num[slen] = '\0';

                char status_line[128];
                snprintf(status_line, sizeof(status_line), "HTTP/1.0 %s\r\n", status_num);
                send(fd, status_line, strlen(status_line), 0);

                data_start = eol + eol_len;
                data_len = first_n - (data_start - buf);
            } else {
                const char *default_status = "HTTP/1.0 200 OK\r\n";
                send(fd, default_status, strlen(default_status), 0);
            }
        } else {
            const char *default_status = "HTTP/1.0 200 OK\r\n";
            send(fd, default_status, strlen(default_status), 0);
        }

        if (data_len > 0) {
            send(fd, data_start, data_len, 0);
        }

        // Stream remaining CGI output to socket
        while ((first_n = read(out_pipe[0], buf, sizeof(buf))) > 0) {
            if (send(fd, buf, first_n, 0) < 0) break;
        }
    }
    close(out_pipe[0]);

    // Wait for CGI process to finish
    int status;
    waitpid(pid, &status, 0);
}

static void handle_client(int fd, const char *client_ip) {
    char buf[BUFFER_SIZE];
    ssize_t total = 0;
    char *hdr_end = NULL;
    int hdr_sep_len = 0;

    // Read headers with 5s timeout
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    while (total < BUFFER_SIZE - 1) {
        ssize_t n = read(fd, buf + total, BUFFER_SIZE - 1 - total);
        if (n <= 0) break;
        total += n;
        buf[total] = '\0';
        hdr_end = strstr(buf, "\r\n\r\n");
        if (hdr_end) { hdr_sep_len = 4; break; }
        hdr_end = strstr(buf, "\n\n");
        if (hdr_end) { hdr_sep_len = 2; break; }
    }

    if (!hdr_end) {
        close(fd);
        return;
    }

    // Parse method and URI
    char method[16] = {0};
    char uri[2048] = {0};
    if (sscanf(buf, "%15s %2047s", method, uri) != 2) {
        close(fd);
        return;
    }

    if (strcasecmp(method, "OPTIONS") == 0) {
        send_cors_options(fd);
        close(fd);
        return;
    }

    // Separate URI into path and query string
    char path[1024] = {0};
    char query[2048] = {0};
    char *qmark = strchr(uri, '?');
    if (qmark) {
        size_t plen = qmark - uri;
        if (plen >= sizeof(path)) plen = sizeof(path) - 1;
        strncpy(path, uri, plen);
        path[plen] = '\0';
        strncpy(query, qmark + 1, sizeof(query) - 1);
    } else {
        strncpy(path, uri, sizeof(path) - 1);
    }

    // Parse Content-Length and Content-Type from headers
    long content_length_l = 0;
    char content_type[256] = {0};
    char *cl_pos = strcasestr(buf, "Content-Length:");
    if (cl_pos && cl_pos < hdr_end) {
        content_length_l = strtol(cl_pos + 15, NULL, 10);
        if (content_length_l < 0) content_length_l = 0;
        if (content_length_l > MAX_CONTENT_LEN) {
            send_error_resp(fd, "413 Payload Too Large", "Payload Too Large (max 1MB)");
            close(fd);
            return;
        }
    }
    int content_length = (int)content_length_l;

    char *ct_pos = strcasestr(buf, "Content-Type:");
    if (ct_pos && ct_pos < hdr_end) {
        char *eol = strstr(ct_pos, "\r\n");
        if (!eol) eol = strchr(ct_pos, '\n');
        if (eol) {
            const char *val = ct_pos + 13;
            while (*val == ' ' || *val == '\t') val++;
            size_t vlen = eol - val;
            if (vlen >= sizeof(content_type)) vlen = sizeof(content_type) - 1;
            strncpy(content_type, val, vlen);
            content_type[vlen] = '\0';
        }
    }

    // Check if CGI request
    if (strncmp(path, "/cgi-bin/", 9) == 0) {
        int body_offset = (int)((hdr_end - buf) + hdr_sep_len);
        int initial_body_len = (int)total - body_offset;
        if (initial_body_len < 0) initial_body_len = 0;
        const char *body_ptr = buf + body_offset;

        // Parse Authorization header
        char auth_hdr[512] = {0};
        char *auth_pos = strcasestr(buf, "Authorization:");
        if (auth_pos && auth_pos < hdr_end) {
            char *eol = strstr(auth_pos, "\r\n");
            if (!eol) eol = strchr(auth_pos, '\n');
            if (eol) {
                const char *val = auth_pos + 14;
                while (*val == ' ' || *val == '\t') val++;
                size_t vlen = eol - val;
                if (vlen >= sizeof(auth_hdr)) vlen = sizeof(auth_hdr) - 1;
                strncpy(auth_hdr, val, vlen);
                auth_hdr[vlen] = '\0';
            }
        }

        handle_cgi(fd, method, path, query, content_type, content_length,
                   auth_hdr,
                   body_ptr, initial_body_len, client_ip);
    } else {
        handle_static(fd, path);
    }

    close(fd);
}

static void sigchld_wakeup(int sig) {
    (void)sig;
}

static void reap_children(pid_t *children, int *active_cnt) {
    for (int i = 0; i < MAX_CHILDREN; i++) {
        if (children[i] > 0) {
            pid_t r = waitpid(children[i], NULL, WNOHANG);
            if (r > 0 || (r < 0 && errno == ECHILD)) {
                children[i] = 0;
                if (*active_cnt > 0) (*active_cnt)--;
            }
        }
    }
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    if (argc > 1) {
        port = atoi(argv[1]);
        if (port <= 0 || port > 65535) port = DEFAULT_PORT;
    }

    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_wakeup;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_NOCLDSTOP; /* Intentionally omit SA_RESTART so accept() wakes and reaps zombies */
    sigaction(SIGCHLD, &sa, NULL);

    int srv_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(srv_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = INADDR_ANY;

    if (bind(srv_fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("bind");
        close(srv_fd);
        return 1;
    }

    if (listen(srv_fd, 32) < 0) {
        perror("listen");
        close(srv_fd);
        return 1;
    }

    pid_t children[MAX_CHILDREN] = {0};
    int active_cnt = 0;

    while (1) {
        reap_children(children, &active_cnt);

        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);
        int conn_fd = accept(srv_fd, (struct sockaddr *)&cli_addr, &cli_len);
        if (conn_fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            sleep(1);
            continue;
        }

        reap_children(children, &active_cnt);
        if (active_cnt >= MAX_CHILDREN) {
            usleep(20000);
            reap_children(children, &active_cnt);
        }

        int slot = -1;
        for (int i = 0; i < MAX_CHILDREN; i++) {
            if (children[i] == 0) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            send_error_resp(conn_fd, "503 Service Unavailable", "Server busy");
            close(conn_fd);
            continue;
        }

        char client_ip[INET_ADDRSTRLEN] = "127.0.0.1";
        inet_ntop(AF_INET, &cli_addr.sin_addr, client_ip, sizeof(client_ip));

        pid_t pid = fork();
        if (pid == 0) {
            signal(SIGCHLD, SIG_DFL);
            close(srv_fd);
            handle_client(conn_fd, client_ip);
            _exit(0);
        } else if (pid > 0) {
            children[slot] = pid;
            active_cnt++;
        }

        close(conn_fd);
    }

    close(srv_fd);
    return 0;
}

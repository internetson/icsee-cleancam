#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <poll.h>
#include <stdint.h>
#include <ctype.h>
#include <math.h>
#include <time.h>

#define RTSP_PORT 554
#define DVRIP_PORT 34567
#define ONVIF_PORT 8899
#define WSD_PORT 3702
#define MAX_CLIENTS 16
#define MAX_PAYLOAD 1350
#define BUFFER_SIZE (512 * 1024)
#define NUM_STREAMS 2

typedef struct {
    int fd;
    volatile int in_use;        // slot is allocated to an active client thread
    volatile int active;        // RTSP connection is active
    volatile int is_playing;    // RTSP PLAY state
    volatile int broken;        // network error / socket closed
    int rtp_interleaved;
    int stream_id; // 0 = Main (1080P), 1 = Sub (D1)
    uint32_t session_id;
    struct sockaddr_in addr;
    char client_ip[32];
    uint16_t client_rtp_port;
    uint16_t client_rtcp_port;
    int udp_rtp_fd;
    int video_setup;            // track0 negotiated
    int audio_setup;            // track1 negotiated
    int video_channel;          // interleaved channel for video (default 0)
    int audio_channel;          // interleaved channel for audio (default 2)
    uint16_t client_audio_rtp_port;
    uint16_t client_audio_rtcp_port;
    int udp_audio_rtp_fd;
    int authenticated;          // 1 = Client authenticated via RTSP Basic Auth
    uint16_t video_rtp_seq;
    uint16_t audio_rtp_seq;
    pthread_mutex_t send_lock;
} client_t;

static client_t clients[MAX_CLIENTS];
static pthread_mutex_t clients_lock = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
    uint16_t rtp_seq;
    uint32_t rtp_timestamp;
    uint32_t rtp_ssrc;
    pthread_mutex_t lock;
} audio_info_t;

static audio_info_t g_audio = {
    .rtp_seq = 100,
    .rtp_timestamp = 1000,
    .rtp_ssrc = 0x12345678,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

typedef enum {
    CODEC_H264 = 0,
    CODEC_H265 = 1
} video_codec_t;

typedef struct {
    int stream_id;
    const char *name;
    video_codec_t codec;
    uint16_t rtp_seq;
    uint32_t rtp_timestamp;
    uint32_t rtp_ssrc;
    uint8_t cached_vps[512];
    size_t cached_vps_len;
    uint8_t cached_sps[512];
    size_t cached_sps_len;
    uint8_t cached_pps[512];
    size_t cached_pps_len;
    struct timespec last_frame_ts;
    int has_last_ts;
    pthread_mutex_t lock;
} stream_info_t;

static stream_info_t streams[NUM_STREAMS];

// Default parameter sets for GK7201V300 1080p H.264 (Main)
static const uint8_t default_main_h264_sps[] = {
    0x67, 0x4d, 0x00, 0x29, 0x96, 0x35, 0x40, 0xf0, 0x04, 0x4f, 0xcb, 0x37, 0x01, 0x01, 0x01, 0x40,
    0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x03, 0x03, 0x21
};
static const uint8_t default_main_h264_pps[] = {
    0x68, 0xee, 0x31, 0xb2
};

// Default parameter sets for Sub Stream H.264 (D1)
static const uint8_t default_sub_h264_sps[] = {
    0x67, 0x4d, 0x00, 0x1e, 0x96, 0x35, 0x41, 0x90, 0x73, 0x4d, 0xc0, 0x40, 0x40, 0x50, 0x00, 0x00,
    0x03, 0x00, 0x10, 0x00, 0x00, 0x03, 0x00, 0xc8, 0x40
};
static const uint8_t default_sub_h264_pps[] = {
    0x68, 0xee, 0x31, 0xb2
};

// Default parameter sets for GK7201V300 1080p H.265 (Main)
static const uint8_t default_main_vps[] = {
    0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0xb0, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x03, 0x00, 0x7b, 0xac, 0x0c, 0x00, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00,
    0x32, 0xa8
};
static const uint8_t default_main_sps[] = {
    0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0xb0, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03,
    0x00, 0x7b, 0xa0, 0x03, 0xc0, 0x80, 0x10, 0xe5, 0x8d, 0xae, 0xe4, 0xcb, 0xf3, 0x70, 0x10, 0x10,
    0x10, 0x40, 0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x03, 0x03, 0x28, 0x40
};
static const uint8_t default_main_pps[] = {
    0x44, 0x01, 0xc0, 0xf2, 0xb0, 0x3b, 0x34
};

// Default parameter sets for Sub Stream H.265 (D1)
static const uint8_t default_sub_vps[] = {
    0x40, 0x01, 0x0c, 0x01, 0xff, 0xff, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0xb0, 0x00, 0x00, 0x03,
    0x00, 0x00, 0x03, 0x00, 0x7b, 0xac, 0x0c, 0x00, 0x00, 0x03, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00,
    0x32, 0xa8
};
static const uint8_t default_sub_sps[] = {
    0x42, 0x01, 0x01, 0x01, 0x60, 0x00, 0x00, 0x03, 0x00, 0xb0, 0x00, 0x00, 0x03, 0x00, 0x00, 0x03,
    0x00, 0x7b, 0xa0, 0x03, 0xc0, 0x80, 0x10, 0xe5, 0x8d, 0xae, 0xe4, 0xcb, 0xf3, 0x70, 0x10, 0x10,
    0x10, 0x40, 0x00, 0x00, 0x03, 0x00, 0x40, 0x00, 0x00, 0x03, 0x03, 0x28, 0x40
};
static const uint8_t default_sub_pps[] = {
    0x44, 0x01, 0xc0, 0xf2, 0xb0, 0x3b, 0x34
};

static int dvrip_running = 1;
static char my_ip[32] = "10.10.11.155";

static void get_local_ip(char *out_ip, size_t max_len) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock >= 0) {
        // Method 1: Connect UDP socket to gateway / DNS to resolve primary outbound IP
        const char *gateways[] = {"10.10.11.1", "192.168.1.1", "8.8.8.8", NULL};
        for (int i = 0; gateways[i]; i++) {
            struct sockaddr_in target;
            memset(&target, 0, sizeof(target));
            target.sin_family = AF_INET;
            target.sin_addr.s_addr = inet_addr(gateways[i]);
            target.sin_port = htons(80);
            if (connect(sock, (struct sockaddr *)&target, sizeof(target)) == 0) {
                struct sockaddr_in laddr;
                socklen_t llen = sizeof(laddr);
                if (getsockname(sock, (struct sockaddr *)&laddr, &llen) == 0) {
                    char *str = inet_ntoa(laddr.sin_addr);
                    if (str && strcmp(str, "0.0.0.0") != 0 && strncmp(str, "127.", 4) != 0) {
                        strncpy(out_ip, str, max_len - 1);
                        out_ip[max_len - 1] = '\0';
                        close(sock);
                        return;
                    }
                }
            }
        }
        // Method 2: Interface scan (eth2, wlan0, ra0, eth0)
        struct ifreq ifr;
        const char *ifnames[] = {"eth2", "wlan0", "ra0", "eth0", NULL};
        for (int i = 0; ifnames[i]; i++) {
            memset(&ifr, 0, sizeof(ifr));
            strncpy(ifr.ifr_name, ifnames[i], IFNAMSIZ - 1);
            if (ioctl(sock, SIOCGIFADDR, &ifr) == 0) {
                struct sockaddr_in *sin = (struct sockaddr_in *)&ifr.ifr_addr;
                char *ip_str = inet_ntoa(sin->sin_addr);
                if (ip_str && strcmp(ip_str, "0.0.0.0") != 0 && strncmp(ip_str, "127.", 4) != 0 && strcmp(ip_str, "192.168.1.10") != 0) {
                    strncpy(out_ip, ip_str, max_len - 1);
                    out_ip[max_len - 1] = '\0';
                    close(sock);
                    return;
                }
            }
        }
        close(sock);
    }
}

static void get_local_mac(char *out_mac, size_t max_len) {
    const char *ifnames[] = {"eth2", "wlan0", "ra0", "eth0", NULL};
    for (int i = 0; ifnames[i]; i++) {
        char path[64];
        snprintf(path, sizeof(path), "/sys/class/net/%s/address", ifnames[i]);
        FILE *f = fopen(path, "r");
        if (f) {
            char buf[32] = {0};
            if (fgets(buf, sizeof(buf), f)) {
                char *nl = strpbrk(buf, "\r\n");
                if (nl) *nl = '\0';
                if (strlen(buf) >= 11 && strcmp(buf, "00:00:00:00:00:00") != 0) {
                    strncpy(out_mac, buf, max_len - 1);
                    out_mac[max_len - 1] = '\0';
                    fclose(f);
                    return;
                }
            }
            fclose(f);
        }
    }
    strncpy(out_mac, "00:12:34:56:78:9A", max_len - 1);
    out_mac[max_len - 1] = '\0';
}

static void base64_encode(const uint8_t *data, size_t len, char *out) {
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0, j = 0;
    while (i < len) {
        uint32_t a = data[i++];
        uint32_t b = (i < len) ? data[i++] : 0;
        uint32_t c = (i < len) ? data[i++] : 0;
        uint32_t triple = (a << 16) | (b << 8) | c;
        out[j++] = tbl[(triple >> 18) & 0x3F];
        out[j++] = tbl[(triple >> 12) & 0x3F];
        out[j++] = (i > len + 1) ? '=' : tbl[(triple >> 6) & 0x3F];
        out[j++] = (i > len) ? '=' : tbl[triple & 0x3F];
    }
    out[j] = '\0';
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

typedef struct {
    int enabled;
    char username[64];
    char password[64];
    time_t last_check;
} stream_auth_cfg_t;

static stream_auth_cfg_t g_stream_auth = {0, "stream", "stream123", 0};
static char g_admin_user[64] = "admin";
static char g_admin_pass[64] = "admin";

static void reload_auth_configs(void) {
    time_t now = time(NULL);
    if (now - g_stream_auth.last_check < 2 && access("/var/tmp/stream_auth.reload", F_OK) != 0) {
        return;
    }
    unlink("/var/tmp/stream_auth.reload");
    g_stream_auth.last_check = now;

    // Load stream auth
    FILE *f = fopen("/mnt/mtd/Config/stream_auth.json", "r");
    if (f) {
        char buf[1024] = {0};
        fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);

        char *en = strstr(buf, "\"enabled\"");
        if (en) {
            char *col = strchr(en, ':');
            if (col) {
                while (*col == ':' || *col == ' ' || *col == '\t') col++;
                if (*col == '1' || strncmp(col, "true", 4) == 0) g_stream_auth.enabled = 1;
                else if (*col == '0' || strncmp(col, "false", 5) == 0) g_stream_auth.enabled = 0;
            }
        }

        char *u = strstr(buf, "\"username\"");
        if (u) {
            char *col = strchr(u, ':');
            if (col) {
                char *q1 = strchr(col, '"');
                if (q1) {
                    char *q2 = strchr(q1 + 1, '"');
                    if (q2) {
                        size_t l = q2 - (q1 + 1);
                        if (l >= sizeof(g_stream_auth.username)) l = sizeof(g_stream_auth.username) - 1;
                        strncpy(g_stream_auth.username, q1 + 1, l);
                        g_stream_auth.username[l] = '\0';
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
                        if (l >= sizeof(g_stream_auth.password)) l = sizeof(g_stream_auth.password) - 1;
                        strncpy(g_stream_auth.password, q1 + 1, l);
                        g_stream_auth.password[l] = '\0';
                    }
                }
            }
        }
    } else {
        g_stream_auth.enabled = 0;
    }

    // Load admin credentials fallback
    FILE *fa = fopen("/mnt/mtd/Config/web_auth.json", "r");
    if (fa) {
        char buf[1024] = {0};
        fread(buf, 1, sizeof(buf) - 1, fa);
        fclose(fa);

        char *u = strstr(buf, "\"username\"");
        if (u) {
            char *col = strchr(u, ':');
            if (col) {
                char *q1 = strchr(col, '"');
                if (q1) {
                    char *q2 = strchr(q1 + 1, '"');
                    if (q2) {
                        size_t l = q2 - (q1 + 1);
                        if (l >= sizeof(g_admin_user)) l = sizeof(g_admin_user) - 1;
                        strncpy(g_admin_user, q1 + 1, l);
                        g_admin_user[l] = '\0';
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
                        if (l >= sizeof(g_admin_pass)) l = sizeof(g_admin_pass) - 1;
                        strncpy(g_admin_pass, q1 + 1, l);
                        g_admin_pass[l] = '\0';
                    }
                }
            }
        }
    }
}

static int verify_rtsp_auth(const char *req, const char *uri) {
    reload_auth_configs();
    if (!g_stream_auth.enabled) return 1; // Auth not required

    // 1. Check HTTP/RTSP "Authorization: Basic ..."
    const char *auth_hdr = strcasestr(req, "Authorization: Basic ");
    if (auth_hdr) {
        const char *b64 = auth_hdr + 21;
        while (*b64 == ' ') b64++;
        char b64_tok[256] = {0};
        size_t bi = 0;
        while (*b64 && !isspace((unsigned char)*b64) && bi < sizeof(b64_tok) - 1) {
            b64_tok[bi++] = *b64++;
        }
        b64_tok[bi] = '\0';

        char decoded[256] = {0};
        base64_decode(b64_tok, decoded, sizeof(decoded));
        char *colon = strchr(decoded, ':');
        if (colon) {
            *colon = '\0';
            const char *u = decoded;
            const char *p = colon + 1;
            if (strcmp(u, g_stream_auth.username) == 0 && strcmp(p, g_stream_auth.password) == 0) {
                return 1;
            }
            if (strcmp(u, g_admin_user) == 0 && strcmp(p, g_admin_pass) == 0) {
                return 1;
            }
        }
    }

    // 2. Check embedded credentials in URI: rtsp://user:pass@...
    const char *at = strchr(uri, '@');
    if (at) {
        const char *proto = strstr(uri, "://");
        const char *start = proto ? (proto + 3) : uri;
        if (start < at) {
            char creds[128] = {0};
            size_t clen = at - start;
            if (clen < sizeof(creds)) {
                strncpy(creds, start, clen);
                creds[clen] = '\0';
                char *colon = strchr(creds, ':');
                if (colon) {
                    *colon = '\0';
                    const char *u = creds;
                    const char *p = colon + 1;
                    if (strcmp(u, g_stream_auth.username) == 0 && strcmp(p, g_stream_auth.password) == 0) {
                        return 1;
                    }
                    if (strcmp(u, g_admin_user) == 0 && strcmp(p, g_admin_pass) == 0) {
                        return 1;
                    }
                }
            }
        }
    }

    return 0; // Not authenticated
}

static void send_rtp_packet_to_client(client_t *c, const uint8_t *rtp_pkt, size_t pkt_len) {
    if (!c->in_use || !c->active || !c->is_playing || c->broken || !c->video_setup) return;
    if (pthread_mutex_trylock(&c->send_lock) != 0) {
        return; // Don't block video streaming thread if this client's send buffer/mutex is busy!
    }
    if (!c->in_use || !c->active || !c->is_playing || c->broken || !c->video_setup) {
        pthread_mutex_unlock(&c->send_lock);
        return;
    }
    if (c->rtp_interleaved) {
        uint8_t hdr[4];
        hdr[0] = '$';
        hdr[1] = (uint8_t)c->video_channel;
        hdr[2] = (pkt_len >> 8) & 0xFF;
        hdr[3] = pkt_len & 0xFF;
        struct iovec iov[2];
        iov[0].iov_base = hdr;
        iov[0].iov_len = 4;
        iov[1].iov_base = (void *)rtp_pkt;
        iov[1].iov_len = pkt_len;
        struct msghdr msg = {0};
        msg.msg_iov = iov;
        msg.msg_iovlen = 2;
        if (sendmsg(c->fd, &msg, MSG_NOSIGNAL) < 0) {
            c->broken = 1;
            c->is_playing = 0;
        }
    } else if (c->client_rtp_port > 0 && c->udp_rtp_fd > 0) {
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(c->client_rtp_port);
        sin.sin_addr = c->addr.sin_addr;
        sendto(c->udp_rtp_fd, rtp_pkt, pkt_len, MSG_NOSIGNAL, (struct sockaddr *)&sin, sizeof(sin));
    }
    pthread_mutex_unlock(&c->send_lock);
}

static void send_rtp_audio_to_client(client_t *c, const uint8_t *rtp_pkt, size_t pkt_len) {
    if (!c->in_use || !c->active || !c->is_playing || c->broken || !c->audio_setup || pkt_len < 12 || pkt_len > 12 + 1024) return;
    if (pthread_mutex_trylock(&c->send_lock) != 0) {
        return;
    }
    if (!c->in_use || !c->active || !c->is_playing || c->broken || !c->audio_setup) {
        pthread_mutex_unlock(&c->send_lock);
        return;
    }
    uint8_t client_pkt[12 + 1024];
    memcpy(client_pkt, rtp_pkt, pkt_len);
    uint16_t seq = __atomic_fetch_add(&c->audio_rtp_seq, 1, __ATOMIC_RELAXED);
    client_pkt[2] = (seq >> 8) & 0xFF;
    client_pkt[3] = seq & 0xFF;

    if (c->rtp_interleaved) {
        uint8_t hdr[4];
        hdr[0] = '$';
        hdr[1] = (uint8_t)c->audio_channel;
        hdr[2] = (pkt_len >> 8) & 0xFF;
        hdr[3] = pkt_len & 0xFF;
        struct iovec iov[2];
        iov[0].iov_base = hdr;
        iov[0].iov_len = 4;
        iov[1].iov_base = (void *)client_pkt;
        iov[1].iov_len = pkt_len;
        struct msghdr msg = {0};
        msg.msg_iov = iov;
        msg.msg_iovlen = 2;
        if (sendmsg(c->fd, &msg, MSG_NOSIGNAL) < 0) {
            c->broken = 1;
            c->is_playing = 0;
        }
    } else if (c->client_audio_rtp_port > 0 && c->udp_audio_rtp_fd > 0) {
        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(c->client_audio_rtp_port);
        sin.sin_addr = c->addr.sin_addr;
        sendto(c->udp_audio_rtp_fd, client_pkt, pkt_len, MSG_NOSIGNAL, (struct sockaddr *)&sin, sizeof(sin));
    }
    pthread_mutex_unlock(&c->send_lock);
}

static void broadcast_audio(const uint8_t *audio_data, size_t audio_len) {
    if (!audio_data || audio_len == 0) return;

    pthread_mutex_lock(&g_audio.lock);
    g_audio.rtp_seq++;
    uint16_t seq = g_audio.rtp_seq;
    uint32_t ts = g_audio.rtp_timestamp;
    g_audio.rtp_timestamp += (uint32_t)audio_len;
    uint32_t ssrc = g_audio.rtp_ssrc;
    pthread_mutex_unlock(&g_audio.lock);

    uint8_t rtp_pkt[12 + 1024];
    if (12 + audio_len > sizeof(rtp_pkt)) return;

    rtp_pkt[0] = 0x80;
    rtp_pkt[1] = 8; // PT 8 = PCMA (G.711 A-law)
    rtp_pkt[2] = (seq >> 8) & 0xFF;
    rtp_pkt[3] = seq & 0xFF;
    rtp_pkt[4] = (ts >> 24) & 0xFF;
    rtp_pkt[5] = (ts >> 16) & 0xFF;
    rtp_pkt[6] = (ts >> 8) & 0xFF;
    rtp_pkt[7] = ts & 0xFF;
    rtp_pkt[8] = (ssrc >> 24) & 0xFF;
    rtp_pkt[9] = (ssrc >> 16) & 0xFF;
    rtp_pkt[10] = (ssrc >> 8) & 0xFF;
    rtp_pkt[11] = ssrc & 0xFF;
    memcpy(rtp_pkt + 12, audio_data, audio_len);

    int active_slots[MAX_CLIENTS];
    int n_active = 0;

    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].in_use && clients[i].active && clients[i].is_playing && !clients[i].broken && clients[i].audio_setup) {
            active_slots[n_active++] = i;
        }
    }
    pthread_mutex_unlock(&clients_lock);

    for (int i = 0; i < n_active; i++) {
        send_rtp_audio_to_client(&clients[active_slots[i]], rtp_pkt, 12 + audio_len);
    }
}

static void send_nal_to_client(client_t *c, const uint8_t *nal, size_t len) {
    if (!c->in_use || !c->active || !c->is_playing || c->broken || len < 2) return;
    int sid = c->stream_id;
    if (sid < 0 || sid >= NUM_STREAMS) sid = 0;

    video_codec_t codec;
    pthread_mutex_lock(&streams[sid].lock);
    codec = streams[sid].codec;
    pthread_mutex_unlock(&streams[sid].lock);

    uint8_t pkt_buf[12 + 4 + MAX_PAYLOAD];

    if (codec == CODEC_H264) {
        uint8_t nal_type = nal[0] & 0x1F;
        if (len <= MAX_PAYLOAD) {
            pthread_mutex_lock(&streams[sid].lock);
            uint32_t ts = streams[sid].rtp_timestamp;
            uint32_t ssrc = streams[sid].rtp_ssrc;
            pthread_mutex_unlock(&streams[sid].lock);
            uint16_t seq = __atomic_fetch_add(&c->video_rtp_seq, 1, __ATOMIC_RELAXED);

            int marker = (nal_type == 1 || nal_type == 5) ? 1 : 0;
            pkt_buf[0] = 0x80;
            pkt_buf[1] = (marker << 7) | 96;
            pkt_buf[2] = (seq >> 8) & 0xFF;
            pkt_buf[3] = seq & 0xFF;
            pkt_buf[4] = (ts >> 24) & 0xFF;
            pkt_buf[5] = (ts >> 16) & 0xFF;
            pkt_buf[6] = (ts >> 8) & 0xFF;
            pkt_buf[7] = ts & 0xFF;
            pkt_buf[8] = (ssrc >> 24) & 0xFF;
            pkt_buf[9] = (ssrc >> 16) & 0xFF;
            pkt_buf[10] = (ssrc >> 8) & 0xFF;
            pkt_buf[11] = ssrc & 0xFF;

            memcpy(pkt_buf + 12, nal, len);
            send_rtp_packet_to_client(c, pkt_buf, 12 + len);
        } else {
            uint8_t fu_ind = (nal[0] & 0xE0) | 28; // FU-A indicator
            const uint8_t *payload = nal + 1;
            size_t payload_len = len - 1;
            size_t offset = 0;

            while (offset < payload_len && !c->broken) {
                size_t chunk_len = payload_len - offset;
                if (chunk_len > MAX_PAYLOAD) chunk_len = MAX_PAYLOAD;

                int is_start = (offset == 0);
                int is_end = (offset + chunk_len >= payload_len);
                uint8_t fu_hdr = (is_start << 7) | (is_end << 6) | nal_type;
                int marker = (is_end && (nal_type == 1 || nal_type == 5)) ? 1 : 0;

                pthread_mutex_lock(&streams[sid].lock);
                uint32_t ts = streams[sid].rtp_timestamp;
                uint32_t ssrc = streams[sid].rtp_ssrc;
                pthread_mutex_unlock(&streams[sid].lock);
                uint16_t seq = __atomic_fetch_add(&c->video_rtp_seq, 1, __ATOMIC_RELAXED);

                pkt_buf[0] = 0x80;
                pkt_buf[1] = (marker << 7) | 96;
                pkt_buf[2] = (seq >> 8) & 0xFF;
                pkt_buf[3] = seq & 0xFF;
                pkt_buf[4] = (ts >> 24) & 0xFF;
                pkt_buf[5] = (ts >> 16) & 0xFF;
                pkt_buf[6] = (ts >> 8) & 0xFF;
                pkt_buf[7] = ts & 0xFF;
                pkt_buf[8] = (ssrc >> 24) & 0xFF;
                pkt_buf[9] = (ssrc >> 16) & 0xFF;
                pkt_buf[10] = (ssrc >> 8) & 0xFF;
                pkt_buf[11] = ssrc & 0xFF;

                pkt_buf[12] = fu_ind;
                pkt_buf[13] = fu_hdr;
                memcpy(pkt_buf + 14, payload + offset, chunk_len);
                send_rtp_packet_to_client(c, pkt_buf, 14 + chunk_len);
                if (!c->rtp_interleaved) usleep(20);
                offset += chunk_len;
            }
        }
    } else { // CODEC_H265
        uint8_t nal_type = (nal[0] >> 1) & 0x3F;
        uint8_t orig_tid = nal[1] & 0x07;
        if (orig_tid == 0) orig_tid = 1; // RFC 7798: TID must be >= 1

        if (len <= MAX_PAYLOAD) {
            pthread_mutex_lock(&streams[sid].lock);
            uint32_t ts = streams[sid].rtp_timestamp;
            uint32_t ssrc = streams[sid].rtp_ssrc;
            pthread_mutex_unlock(&streams[sid].lock);
            uint16_t seq = __atomic_fetch_add(&c->video_rtp_seq, 1, __ATOMIC_RELAXED);

            int marker = (nal_type == 1 || nal_type == 19 || nal_type == 20) ? 1 : 0;
            pkt_buf[0] = 0x80;
            pkt_buf[1] = (marker << 7) | 96;
            pkt_buf[2] = (seq >> 8) & 0xFF;
            pkt_buf[3] = seq & 0xFF;
            pkt_buf[4] = (ts >> 24) & 0xFF;
            pkt_buf[5] = (ts >> 16) & 0xFF;
            pkt_buf[6] = (ts >> 8) & 0xFF;
            pkt_buf[7] = ts & 0xFF;
            pkt_buf[8] = (ssrc >> 24) & 0xFF;
            pkt_buf[9] = (ssrc >> 16) & 0xFF;
            pkt_buf[10] = (ssrc >> 8) & 0xFF;
            pkt_buf[11] = ssrc & 0xFF;

            memcpy(pkt_buf + 12, nal, len);
            pkt_buf[12] = (nal_type << 1);
            pkt_buf[13] = orig_tid;

            send_rtp_packet_to_client(c, pkt_buf, 12 + len);
        } else {
            uint8_t fu_ind[2];
            fu_ind[0] = (49 << 1); // F=0, Type=49 (FU), LayerId_high=0
            fu_ind[1] = orig_tid;  // LayerId_low=0, TID=orig_tid (>=1)

            const uint8_t *payload = nal + 2;
            size_t payload_len = len - 2;
            size_t offset = 0;

            while (offset < payload_len && !c->broken) {
                size_t chunk_len = payload_len - offset;
                if (chunk_len > MAX_PAYLOAD) chunk_len = MAX_PAYLOAD;

                int is_start = (offset == 0);
                int is_end = (offset + chunk_len >= payload_len);
                uint8_t fu_hdr = (is_start << 7) | (is_end << 6) | nal_type;
                int marker = (is_end && (nal_type == 1 || nal_type == 19 || nal_type == 20)) ? 1 : 0;

                pthread_mutex_lock(&streams[sid].lock);
                uint32_t ts = streams[sid].rtp_timestamp;
                uint32_t ssrc = streams[sid].rtp_ssrc;
                pthread_mutex_unlock(&streams[sid].lock);
                uint16_t seq = __atomic_fetch_add(&c->video_rtp_seq, 1, __ATOMIC_RELAXED);

                pkt_buf[0] = 0x80;
                pkt_buf[1] = (marker << 7) | 96;
                pkt_buf[2] = (seq >> 8) & 0xFF;
                pkt_buf[3] = seq & 0xFF;
                pkt_buf[4] = (ts >> 24) & 0xFF;
                pkt_buf[5] = (ts >> 16) & 0xFF;
                pkt_buf[6] = (ts >> 8) & 0xFF;
                pkt_buf[7] = ts & 0xFF;
                pkt_buf[8] = (ssrc >> 24) & 0xFF;
                pkt_buf[9] = (ssrc >> 16) & 0xFF;
                pkt_buf[10] = (ssrc >> 8) & 0xFF;
                pkt_buf[11] = ssrc & 0xFF;

                pkt_buf[12] = fu_ind[0];
                pkt_buf[13] = fu_ind[1];
                pkt_buf[14] = fu_hdr;
                memcpy(pkt_buf + 15, payload + offset, chunk_len);
                send_rtp_packet_to_client(c, pkt_buf, 15 + chunk_len);
                if (!c->rtp_interleaved) usleep(20);
                offset += chunk_len;
            }
        }
    }
}

static void broadcast_nal(int sid, const uint8_t *nal, size_t len) {
    if (!nal || len < 2 || sid < 0 || sid >= NUM_STREAMS) return;

    uint8_t b0 = nal[0];
    uint8_t h264_type = b0 & 0x1F;
    uint8_t h265_type = (b0 >> 1) & 0x3F;

    if (h265_type == 32) { // H.265 VPS
        pthread_mutex_lock(&streams[sid].lock);
        streams[sid].codec = CODEC_H265;
        if (len <= sizeof(streams[sid].cached_vps)) {
            memcpy(streams[sid].cached_vps, nal, len);
            streams[sid].cached_vps[0] = (32 << 1);
            streams[sid].cached_vps[1] = 1;
            streams[sid].cached_vps_len = len;
        }
        pthread_mutex_unlock(&streams[sid].lock);
    } else if (h265_type == 33 && (b0 & 0x80) == 0 && (nal[1] & 0xF8) == 0) { // H.265 SPS
        pthread_mutex_lock(&streams[sid].lock);
        streams[sid].codec = CODEC_H265;
        if (len <= sizeof(streams[sid].cached_sps)) {
            memcpy(streams[sid].cached_sps, nal, len);
            streams[sid].cached_sps[0] = (33 << 1);
            streams[sid].cached_sps[1] = 1;
            streams[sid].cached_sps_len = len;
        }
        pthread_mutex_unlock(&streams[sid].lock);
    } else if (h265_type == 34 && (b0 & 0x80) == 0 && (nal[1] & 0xF8) == 0) { // H.265 PPS
        pthread_mutex_lock(&streams[sid].lock);
        if (streams[sid].codec == CODEC_H265) {
            if (len <= sizeof(streams[sid].cached_pps)) {
                memcpy(streams[sid].cached_pps, nal, len);
                streams[sid].cached_pps[0] = (34 << 1);
                streams[sid].cached_pps[1] = 1;
                streams[sid].cached_pps_len = len;
            }
        }
        pthread_mutex_unlock(&streams[sid].lock);
    } else if (h264_type == 7 && (b0 & 0x80) == 0) { // H.264 SPS
        pthread_mutex_lock(&streams[sid].lock);
        streams[sid].codec = CODEC_H264;
        if (len <= sizeof(streams[sid].cached_sps)) {
            memcpy(streams[sid].cached_sps, nal, len);
            streams[sid].cached_sps_len = len;
        }
        pthread_mutex_unlock(&streams[sid].lock);
    } else if (h264_type == 8 && (b0 & 0x80) == 0) { // H.264 PPS
        pthread_mutex_lock(&streams[sid].lock);
        streams[sid].codec = CODEC_H264;
        if (len <= sizeof(streams[sid].cached_pps)) {
            memcpy(streams[sid].cached_pps, nal, len);
            streams[sid].cached_pps_len = len;
        }
        pthread_mutex_unlock(&streams[sid].lock);
    }

    // Take snapshot of active client slot indices under clients_lock
    int active_slots[MAX_CLIENTS];
    int n_active = 0;

    pthread_mutex_lock(&clients_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i].in_use && clients[i].active && clients[i].is_playing && clients[i].stream_id == sid && !clients[i].broken) {
            active_slots[n_active++] = i;
        }
    }
    pthread_mutex_unlock(&clients_lock);

    // Send packets OUTSIDE clients_lock
    for (int i = 0; i < n_active; i++) {
        send_nal_to_client(&clients[active_slots[i]], nal, len);
    }
}

static void send_parameter_sets_to_client(client_t *c) {
    int sid = c->stream_id;
    if (sid < 0 || sid >= NUM_STREAMS) sid = 0;
    uint8_t vps[512], sps[512], pps[512];
    size_t v_len, s_len, p_len;
    video_codec_t codec;

    pthread_mutex_lock(&streams[sid].lock);
    codec = streams[sid].codec;
    v_len = streams[sid].cached_vps_len;
    if (v_len > sizeof(vps)) v_len = sizeof(vps);
    memcpy(vps, streams[sid].cached_vps, v_len);
    s_len = streams[sid].cached_sps_len;
    if (s_len > sizeof(sps)) s_len = sizeof(sps);
    memcpy(sps, streams[sid].cached_sps, s_len);
    p_len = streams[sid].cached_pps_len;
    if (p_len > sizeof(pps)) p_len = sizeof(pps);
    memcpy(pps, streams[sid].cached_pps, p_len);
    pthread_mutex_unlock(&streams[sid].lock);

    if (codec == CODEC_H265 && v_len > 0) {
        send_nal_to_client(c, vps, v_len);
    }
    if (s_len > 0) {
        send_nal_to_client(c, sps, s_len);
    }
    if (p_len > 0) {
        send_nal_to_client(c, pps, p_len);
    }
}

static void process_video_frame(int sid, const uint8_t *data, size_t len) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    pthread_mutex_lock(&streams[sid].lock);
    if (streams[sid].has_last_ts) {
        long long delta_us = (long long)(now.tv_sec - streams[sid].last_frame_ts.tv_sec) * 1000000LL +
                             (long long)(now.tv_nsec - streams[sid].last_frame_ts.tv_nsec) / 1000LL;
        if (delta_us < 1000) delta_us = 1000;
        if (delta_us > 500000) delta_us = 83333; // fallback 12fps on long stall
        streams[sid].rtp_timestamp += (uint32_t)((delta_us * 90LL) / 1000LL);
    } else {
        streams[sid].has_last_ts = 1;
        streams[sid].rtp_timestamp += 7500;
    }
    streams[sid].last_frame_ts = now;
    pthread_mutex_unlock(&streams[sid].lock);

    size_t pos = 0;
    while (pos < len) {
        size_t start = (size_t)-1;
        for (size_t i = pos; i + 2 < len; i++) {
            if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 1) {
                start = i + 3;
                break;
            }
        }
        if (start == (size_t)-1) break;

        size_t end = len;
        for (size_t i = start; i + 2 < len; i++) {
            if (data[i] == 0 && data[i+1] == 0 && data[i+2] == 1) {
                end = i;
                if (end > start && data[end-1] == 0) end--;
                break;
            }
        }

        if (end > start) {
            broadcast_nal(sid, data + start, end - start);
        }
        pos = end;
    }
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

typedef struct {
    int s_cmd;
    uint32_t session_id;
    volatile int running;
} ka_arg_t;

static void *keepalive_thread(void *arg) {
    ka_arg_t *ka = (ka_arg_t *)arg;
    uint32_t seq = 100;
    while (ka->running) {
        for (int i = 0; i < 10 && ka->running; i++) sleep(1);
        if (!ka->running) break;

        char ka_json[128];
        snprintf(ka_json, sizeof(ka_json), "{\"Name\":\"KeepAlive\",\"SessionID\":\"0x%08X\"}\n", ka->session_id);
        uint32_t ka_len = strlen(ka_json) + 1;
        uint8_t hdr[20];
        hdr[0] = 0xFF; hdr[1] = 0x01; hdr[2] = 0; hdr[3] = 0;
        memcpy(hdr + 4, &ka->session_id, 4);
        seq++;
        memcpy(hdr + 8, &seq, 4);
        hdr[12] = 0; hdr[13] = 0;
        uint16_t msg_ka = 1006;
        memcpy(hdr + 14, &msg_ka, 2);
        memcpy(hdr + 16, &ka_len, 4);
        if (send(ka->s_cmd, hdr, 20, MSG_NOSIGNAL) < 0 ||
            send(ka->s_cmd, ka_json, ka_len, MSG_NOSIGNAL) < 0) {
            break;
        }

        // Drain keepalive reply so kernel receive buffer never fills up
        while (1) {
            struct pollfd pfd = { .fd = ka->s_cmd, .events = POLLIN };
            if (poll(&pfd, 1, 100) <= 0 || !(pfd.revents & POLLIN)) break;
            uint8_t drain_buf[512];
            if (recv(ka->s_cmd, drain_buf, sizeof(drain_buf), MSG_DONTWAIT) <= 0) break;
        }
    }
    return NULL;
}

static void *dvrip_stream_thread(void *arg) {
    int sid = (int)(intptr_t)arg;
    const char *stype = (sid == 1) ? "Extra1" : "Main";
    printf("[DVRIP-%s] Starting worker thread for stream %d (%s)...\n", stype, sid, stype);

    while (dvrip_running) {
        int s_cmd = socket(AF_INET, SOCK_STREAM, 0);
        if (s_cmd < 0) { sleep(2); continue; }

        struct timeval tv_cmd = { .tv_sec = 6, .tv_usec = 0 };
        setsockopt(s_cmd, SOL_SOCKET, SO_RCVTIMEO, &tv_cmd, sizeof(tv_cmd));
        setsockopt(s_cmd, SOL_SOCKET, SO_SNDTIMEO, &tv_cmd, sizeof(tv_cmd));

        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = AF_INET;
        sin.sin_port = htons(DVRIP_PORT);
        sin.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (connect(s_cmd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
            printf("[DVRIP-%s] Connect failed: %s\n", stype, strerror(errno));
            close(s_cmd);
            sleep(2);
            continue;
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

        send(s_cmd, hdr, 20, 0);
        send(s_cmd, login_json, login_len, 0);

        if (recv_all(s_cmd, hdr, 20) < 0) { close(s_cmd); sleep(2); continue; }
        uint32_t resp_len = *(uint32_t *)(hdr + 16);
        if (resp_len == 0 || resp_len > 65536) { close(s_cmd); sleep(2); continue; }
        char *resp_buf = malloc(resp_len + 1);
        if (!resp_buf || recv_all(s_cmd, (uint8_t *)resp_buf, resp_len) < 0) { free(resp_buf); close(s_cmd); sleep(2); continue; }
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
        if (session_id == 0) {
            printf("[DVRIP-%s] Login failed (SessionID=0), retrying...\n", stype);
            close(s_cmd);
            sleep(2);
            continue;
        }
        printf("[DVRIP-%s] Logged in! Session: 0x%08X\n", stype, session_id);

        int s_data = socket(AF_INET, SOCK_STREAM, 0);
        if (s_data < 0) { close(s_cmd); sleep(2); continue; }
        struct timeval tv_data = { .tv_sec = 10, .tv_usec = 0 };
        setsockopt(s_data, SOL_SOCKET, SO_RCVTIMEO, &tv_data, sizeof(tv_data));
        setsockopt(s_data, SOL_SOCKET, SO_SNDTIMEO, &tv_data, sizeof(tv_data));

        if (connect(s_data, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
            close(s_cmd); close(s_data); sleep(2); continue;
        }

        char claim_json[256];
        snprintf(claim_json, sizeof(claim_json),
                 "{\"Name\":\"OPMonitor\",\"SessionID\":\"0x%08X\",\"OPMonitor\":{\"Action\":\"Claim\",\"Parameter\":{\"Channel\":0,\"CombinType\":\"NONE\",\"StreamType\":\"%s\",\"TransMode\":\"TCP\"}}}\n",
                 session_id, stype);
        uint32_t claim_len = strlen(claim_json) + 1;
        hdr[0] = 0xFF; hdr[1] = 0x01; hdr[2] = 0; hdr[3] = 0;
        memcpy(hdr + 4, &session_id, 4);
        memcpy(hdr + 8, &one, 4);
        hdr[12] = 0; hdr[13] = 0;
        uint16_t msg_claim = 1413;
        memcpy(hdr + 14, &msg_claim, 2);
        memcpy(hdr + 16, &claim_len, 4);
        send(s_data, hdr, 20, 0);
        send(s_data, claim_json, claim_len, 0);

        if (recv_all(s_data, hdr, 20) < 0) { close(s_cmd); close(s_data); sleep(2); continue; }
        uint32_t cl_len = *(uint32_t *)(hdr + 16);
        if (cl_len > 65536) { close(s_cmd); close(s_data); sleep(2); continue; }
        if (cl_len > 0) {
            uint8_t *cl_buf = malloc(cl_len);
            if (!cl_buf || recv_all(s_data, cl_buf, cl_len) < 0) { free(cl_buf); close(s_cmd); close(s_data); sleep(2); continue; }
            free(cl_buf);
        }

        char start_json[256];
        snprintf(start_json, sizeof(start_json),
                 "{\"Name\":\"OPMonitor\",\"SessionID\":\"0x%08X\",\"OPMonitor\":{\"Action\":\"Start\",\"Parameter\":{\"Channel\":0,\"CombinType\":\"NONE\",\"StreamType\":\"%s\",\"TransMode\":\"TCP\"}}}\n",
                 session_id, stype);
        uint32_t start_len = strlen(start_json) + 1;
        uint32_t two = 2;
        memcpy(hdr + 8, &two, 4);
        uint16_t msg_start = 1410;
        memcpy(hdr + 14, &msg_start, 2);
        memcpy(hdr + 16, &start_len, 4);
        send(s_cmd, hdr, 20, 0);
        send(s_cmd, start_json, start_len, 0);

        if (recv_all(s_cmd, hdr, 20) < 0) { close(s_cmd); close(s_data); sleep(2); continue; }
        uint32_t st_len = *(uint32_t *)(hdr + 16);
        if (st_len > 65536) { close(s_cmd); close(s_data); sleep(2); continue; }
        if (st_len > 0) {
            uint8_t *st_buf = malloc(st_len);
            if (!st_buf || recv_all(s_cmd, st_buf, st_len) < 0) { free(st_buf); close(s_cmd); close(s_data); sleep(2); continue; }
            free(st_buf);
        }

        printf("[DVRIP-%s] Active and streaming!\n", stype);

        ka_arg_t ka_arg = { .s_cmd = s_cmd, .session_id = session_id, .running = 1 };
        pthread_t ka_tid;
        pthread_create(&ka_tid, NULL, keepalive_thread, &ka_arg);

        uint8_t *ring = malloc(BUFFER_SIZE);
        if (!ring) {
            ka_arg.running = 0;
            pthread_join(ka_tid, NULL);
            close(s_data); close(s_cmd); sleep(2); continue;
        }
        size_t ring_len = 0;

        while (dvrip_running) {
            if (recv_all(s_data, hdr, 20) < 0) {
                printf("[DVRIP-%s] Connection lost!\n", stype);
                break;
            }
            uint32_t chunk_len = *(uint32_t *)(hdr + 16);
            if (chunk_len > BUFFER_SIZE) {
                printf("[DVRIP-%s] Chunk too large: %u\n", stype, chunk_len);
                break;
            }

            if (ring_len + chunk_len > BUFFER_SIZE) {
                ring_len = 0;
            }

            if (recv_all(s_data, ring + ring_len, chunk_len) < 0) break;
            ring_len += chunk_len;

            size_t offset = 0;
            while (ring_len - offset >= 16) {
                if (ring[offset] != 0 || ring[offset+1] != 0 || ring[offset+2] != 1) {
                    size_t next_sync = (size_t)-1;
                    for (size_t i = offset + 1; i + 2 < ring_len; i++) {
                        if (ring[i] == 0 && ring[i+1] == 0 && ring[i+2] == 1) {
                            next_sync = i;
                            break;
                        }
                    }
                    if (next_sync != (size_t)-1) {
                        offset = next_sync;
                    } else {
                        offset = ring_len;
                    }
                    continue;
                }

                uint8_t pType = ring[offset + 3];
                size_t frame_total = 0;
                size_t payload_offset = 0;

                if (pType == 0xFC || pType == 0xFE) {
                    uint32_t dsz = *(uint32_t *)(ring + offset + 12);
                    frame_total = dsz + 16;
                    payload_offset = 16;
                } else if (pType == 0xFD) {
                    uint32_t dsz = *(uint32_t *)(ring + offset + 4);
                    frame_total = dsz + 8;
                    payload_offset = 8;
                } else if (pType == 0xFA || pType == 0xF9) {
                    uint16_t dsz = *(uint16_t *)(ring + offset + 6);
                    frame_total = dsz + 8;
                    payload_offset = 8;
                } else {
                    offset += 4;
                    continue;
                }

                if (ring_len - offset < frame_total) {
                    break;
                }

                if (pType == 0xFC || pType == 0xFE || pType == 0xFD) {
                    process_video_frame(sid, ring + offset + payload_offset, frame_total - payload_offset);
                } else if (pType == 0xFA || pType == 0xF9) {
                    broadcast_audio(ring + offset + payload_offset, frame_total - payload_offset);
                }

                offset += frame_total;
            }

            if (offset > 0) {
                if (offset < ring_len) {
                    memmove(ring, ring + offset, ring_len - offset);
                    ring_len -= offset;
                } else {
                    ring_len = 0;
                }
            }
        }

        ka_arg.running = 0;
        pthread_join(ka_tid, NULL);

        free(ring);
        close(s_data);
        close(s_cmd);
        sleep(2);
    }
    return NULL;
}

static void *rtsp_client_thread(void *arg) {
    int client_idx = (int)(intptr_t)arg;
    client_t *c = &clients[client_idx];
    int cfd = c->fd;
    char client_ip[32];
    strncpy(client_ip, c->client_ip, sizeof(client_ip));
    printf("[RTSP] Client connected: %s (slot %d)\n", client_ip, client_idx);

    char req[4096];
    char resp[4096];

    while (c->active && !c->broken) {
        memset(req, 0, sizeof(req));
        size_t total = 0;
        int got_interleaved = 0;
        while (total < sizeof(req) - 1) {
            ssize_t n = recv(cfd, req + total, 1, 0);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    if (total == 0) {
                        break;
                    }
                    continue;
                }
                goto client_done;
            } else if (n == 0) {
                goto client_done;
            }
            if (total == 0 && (uint8_t)req[0] == '$') {
                uint8_t ihdr[3];
                if (recv_all(cfd, ihdr, 3) < 0) goto client_done;
                uint16_t ilen = ((uint16_t)ihdr[1] << 8) | ihdr[2];
                uint8_t drain[512];
                size_t rem = ilen;
                while (rem > 0) {
                    size_t chunk = rem > sizeof(drain) ? sizeof(drain) : rem;
                    if (recv_all(cfd, drain, chunk) < 0) goto client_done;
                    rem -= chunk;
                }
                got_interleaved = 1;
                break;
            }
            total += n;
            if (total >= 4 && memcmp(req + total - 4, "\r\n\r\n", 4) == 0) break;
        }
        if (got_interleaved || total == 0) continue;

        req[total] = '\0';

        char method[32] = {0}, uri[256] = {0}, ver[32] = {0};
        sscanf(req, "%31s %255s %31s", method, uri, ver);

        int cseq = 1;
        char *cseq_line = strcasestr(req, "CSeq:");
        if (cseq_line) {
            cseq = atoi(cseq_line + 5);
        }

        // Detect requested stream:
        // /live/ch1, /stream1, /sub, /extra1 -> Stream 1 (Sub)
        // /live/ch0, /stream0, /main -> Stream 0 (Main)
        if (strstr(uri, "/ch1") || strstr(uri, "stream1") || strstr(uri, "/sub") || strstr(uri, "extra1") || strstr(uri, "substream")) {
            c->stream_id = 1;
        } else if (strstr(uri, "/ch0") || strstr(uri, "stream0") || strstr(uri, "/main")) {
            c->stream_id = 0;
        }

        int sid = c->stream_id;
        printf("[RTSP] %s %s (CSeq: %d, Stream: %d)\n", method, uri, cseq, sid);

        if (strcmp(method, "OPTIONS") == 0) {
            snprintf(resp, sizeof(resp),
                     "RTSP/1.0 200 OK\r\n"
                     "CSeq: %d\r\n"
                     "Public: OPTIONS, DESCRIBE, SETUP, TEARDOWN, PLAY, PAUSE\r\n\r\n",
                     cseq);
            pthread_mutex_lock(&c->send_lock);
            if (send(cfd, resp, strlen(resp), MSG_NOSIGNAL) < 0) c->broken = 1;
            pthread_mutex_unlock(&c->send_lock);
        } else {
            // Require authentication for DESCRIBE, SETUP, PLAY, etc.
            if (!c->authenticated) {
                if (verify_rtsp_auth(req, uri)) {
                    c->authenticated = 1;
                    printf("[RTSP] Client %s authenticated successfully!\n", client_ip);
                } else {
                    printf("[RTSP] Auth required/failed for client %s (%s)\n", client_ip, method);
                    snprintf(resp, sizeof(resp),
                             "RTSP/1.0 401 Unauthorized\r\n"
                             "CSeq: %d\r\n"
                             "WWW-Authenticate: Basic realm=\"CleanCam\"\r\n\r\n",
                             cseq);
                    pthread_mutex_lock(&c->send_lock);
                    if (send(cfd, resp, strlen(resp), MSG_NOSIGNAL) < 0) c->broken = 1;
                    pthread_mutex_unlock(&c->send_lock);
                    continue;
                }
            }

            if (strcmp(method, "DESCRIBE") == 0) {
            char vps_b64[256] = {0}, sps_b64[256] = {0}, pps_b64[256] = {0};
            video_codec_t codec;
            char profile_lvl[16] = "4d0029";

            pthread_mutex_lock(&streams[sid].lock);
            codec = streams[sid].codec;
            if (codec == CODEC_H265) {
                base64_encode(streams[sid].cached_vps, streams[sid].cached_vps_len, vps_b64);
            }
            base64_encode(streams[sid].cached_sps, streams[sid].cached_sps_len, sps_b64);
            base64_encode(streams[sid].cached_pps, streams[sid].cached_pps_len, pps_b64);
            if (codec == CODEC_H264 && streams[sid].cached_sps_len >= 4) {
                snprintf(profile_lvl, sizeof(profile_lvl), "%02x%02x%02x",
                         streams[sid].cached_sps[1], streams[sid].cached_sps[2], streams[sid].cached_sps[3]);
            }
            pthread_mutex_unlock(&streams[sid].lock);

            char sdp[1024];
            int sdp_len = 0;
            int as_bw = (sid == 1) ? 1024 : 4096;

            if (codec == CODEC_H264) {
                const char *stream_label = (sid == 1) ? "CleanCam H264 Sub (D1)" : "CleanCam H264 Main (1080P)";
                sdp_len = snprintf(sdp, sizeof(sdp),
                                   "v=0\r\n"
                                   "o=- 0 0 IN IP4 0.0.0.0\r\n"
                                   "s=%s\r\n"
                                   "t=0 0\r\n"
                                   "m=video 0 RTP/AVP 96\r\n"
                                   "c=IN IP4 0.0.0.0\r\n"
                                   "b=AS:%d\r\n"
                                   "a=rtpmap:96 H264/90000\r\n"
                                   "a=fmtp:96 packetization-mode=1;profile-level-id=%s;sprop-parameter-sets=%s,%s\r\n"
                                   "a=control:track0\r\n"
                                   "m=audio 0 RTP/AVP 8\r\n"
                                   "c=IN IP4 0.0.0.0\r\n"
                                   "b=AS:64\r\n"
                                   "a=rtpmap:8 PCMA/8000/1\r\n"
                                   "a=control:track1\r\n",
                                   stream_label, as_bw, profile_lvl, sps_b64, pps_b64);
            } else {
                const char *stream_label = (sid == 1) ? "CleanCam H265 Sub (D1)" : "CleanCam H265 Main (1080P)";
                sdp_len = snprintf(sdp, sizeof(sdp),
                                   "v=0\r\n"
                                   "o=- 0 0 IN IP4 0.0.0.0\r\n"
                                   "s=%s\r\n"
                                   "t=0 0\r\n"
                                   "m=video 0 RTP/AVP 96\r\n"
                                   "c=IN IP4 0.0.0.0\r\n"
                                   "b=AS:%d\r\n"
                                   "a=rtpmap:96 H265/90000\r\n"
                                   "a=fmtp:96 sprop-vps=%s;sprop-sps=%s;sprop-pps=%s\r\n"
                                   "a=control:track0\r\n"
                                   "m=audio 0 RTP/AVP 8\r\n"
                                   "c=IN IP4 0.0.0.0\r\n"
                                   "b=AS:64\r\n"
                                   "a=rtpmap:8 PCMA/8000/1\r\n"
                                   "a=control:track1\r\n",
                                   stream_label, as_bw, vps_b64, sps_b64, pps_b64);
            }

            snprintf(resp, sizeof(resp),
                     "RTSP/1.0 200 OK\r\n"
                     "CSeq: %d\r\n"
                     "Content-Type: application/sdp\r\n"
                     "Content-Base: %s/\r\n"
                     "Content-Length: %d\r\n\r\n"
                     "%s",
                     cseq, uri, sdp_len, sdp);
            pthread_mutex_lock(&c->send_lock);
            if (send(cfd, resp, strlen(resp), MSG_NOSIGNAL) < 0) c->broken = 1;
            pthread_mutex_unlock(&c->send_lock);
        } else if (strcmp(method, "SETUP") == 0) {
            char *trans_hdr = strcasestr(req, "Transport:");
            int is_tcp = (trans_hdr && (strcasestr(trans_hdr, "TCP") || strcasestr(trans_hdr, "interleaved")));
            char *cp = trans_hdr ? strcasestr(trans_hdr, "client_port=") : NULL;
            int is_audio = (strstr(uri, "track1") != NULL);

            if (is_tcp || !cp) {
                c->rtp_interleaved = 1;
                int inter_ch = is_audio ? 2 : 0;
                char *il_pos = trans_hdr ? strcasestr(trans_hdr, "interleaved=") : NULL;
                if (il_pos) {
                    inter_ch = atoi(il_pos + 12);
                }
                if (is_audio) {
                    c->audio_setup = 1;
                    c->audio_channel = inter_ch;
                } else {
                    c->video_setup = 1;
                    c->video_channel = inter_ch;
                }
                snprintf(resp, sizeof(resp),
                         "RTSP/1.0 200 OK\r\n"
                         "CSeq: %d\r\n"
                         "Transport: RTP/AVP/TCP;unicast;interleaved=%d-%d\r\n"
                         "Session: %08X\r\n\r\n",
                         cseq, inter_ch, inter_ch + 1, c->session_id);
            } else {
                int rtp_p = 0, rtcp_p = 0;
                sscanf(cp + 12, "%d-%d", &rtp_p, &rtcp_p);
                if (rtcp_p == 0) rtcp_p = rtp_p + 1;

                c->rtp_interleaved = 0;
                int srv_port = 0;

                if (is_audio) {
                    c->audio_setup = 1;
                    c->client_audio_rtp_port = rtp_p;
                    c->client_audio_rtcp_port = rtcp_p;
                    if (c->udp_audio_rtp_fd <= 0) {
                        c->udp_audio_rtp_fd = socket(AF_INET, SOCK_DGRAM, 0);
                        int sndbuf = 128 * 1024;
                        setsockopt(c->udp_audio_rtp_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
                        srv_port = 6972 + client_idx * 4;
                        struct sockaddr_in srv_udp;
                        memset(&srv_udp, 0, sizeof(srv_udp));
                        srv_udp.sin_family = AF_INET;
                        srv_udp.sin_port = htons(srv_port);
                        srv_udp.sin_addr.s_addr = INADDR_ANY;
                        if (bind(c->udp_audio_rtp_fd, (struct sockaddr *)&srv_udp, sizeof(srv_udp)) < 0) {
                            srv_udp.sin_port = 0;
                            bind(c->udp_audio_rtp_fd, (struct sockaddr *)&srv_udp, sizeof(srv_udp));
                            socklen_t slen = sizeof(srv_udp);
                            getsockname(c->udp_audio_rtp_fd, (struct sockaddr *)&srv_udp, &slen);
                            srv_port = ntohs(srv_udp.sin_port);
                        }
                    } else {
                        srv_port = 6972 + client_idx * 4;
                    }
                } else {
                    c->video_setup = 1;
                    c->client_rtp_port = rtp_p;
                    c->client_rtcp_port = rtcp_p;
                    if (c->udp_rtp_fd <= 0) {
                        c->udp_rtp_fd = socket(AF_INET, SOCK_DGRAM, 0);
                        int sndbuf = 256 * 1024;
                        setsockopt(c->udp_rtp_fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, sizeof(sndbuf));
                        srv_port = 6970 + client_idx * 4;
                        struct sockaddr_in srv_udp;
                        memset(&srv_udp, 0, sizeof(srv_udp));
                        srv_udp.sin_family = AF_INET;
                        srv_udp.sin_port = htons(srv_port);
                        srv_udp.sin_addr.s_addr = INADDR_ANY;
                        if (bind(c->udp_rtp_fd, (struct sockaddr *)&srv_udp, sizeof(srv_udp)) < 0) {
                            srv_udp.sin_port = 0;
                            bind(c->udp_rtp_fd, (struct sockaddr *)&srv_udp, sizeof(srv_udp));
                            socklen_t slen = sizeof(srv_udp);
                            getsockname(c->udp_rtp_fd, (struct sockaddr *)&srv_udp, &slen);
                            srv_port = ntohs(srv_udp.sin_port);
                        }
                    } else {
                        srv_port = 6970 + client_idx * 4;
                    }
                }

                snprintf(resp, sizeof(resp),
                         "RTSP/1.0 200 OK\r\n"
                         "CSeq: %d\r\n"
                         "Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=%d-%d\r\n"
                         "Session: %08X\r\n\r\n",
                         cseq, rtp_p, rtcp_p, srv_port, srv_port + 1, c->session_id);
            }
            pthread_mutex_lock(&c->send_lock);
            if (send(cfd, resp, strlen(resp), MSG_NOSIGNAL) < 0) c->broken = 1;
            pthread_mutex_unlock(&c->send_lock);
        } else if (strcmp(method, "PLAY") == 0) {
            pthread_mutex_lock(&streams[sid].lock);
            uint32_t cur_ts = streams[sid].rtp_timestamp;
            pthread_mutex_unlock(&streams[sid].lock);
            uint16_t cur_seq = __atomic_load_n(&c->video_rtp_seq, __ATOMIC_RELAXED);

            pthread_mutex_lock(&g_audio.lock);
            uint32_t cur_ats = g_audio.rtp_timestamp;
            pthread_mutex_unlock(&g_audio.lock);
            uint16_t cur_aseq = __atomic_load_n(&c->audio_rtp_seq, __ATOMIC_RELAXED);

            if (c->audio_setup) {
                snprintf(resp, sizeof(resp),
                         "RTSP/1.0 200 OK\r\n"
                         "CSeq: %d\r\n"
                         "Session: %08X\r\n"
                         "RTP-Info: url=%s/track0;seq=%u;rtptime=%u,url=%s/track1;seq=%u;rtptime=%u\r\n\r\n",
                         cseq, c->session_id, uri, cur_seq, cur_ts, uri, cur_aseq, cur_ats);
            } else {
                snprintf(resp, sizeof(resp),
                         "RTSP/1.0 200 OK\r\n"
                         "CSeq: %d\r\n"
                         "Session: %08X\r\n"
                         "RTP-Info: url=%s/track0;seq=%u;rtptime=%u\r\n\r\n",
                         cseq, c->session_id, uri, cur_seq, cur_ts);
            }
            pthread_mutex_lock(&c->send_lock);
            if (send(cfd, resp, strlen(resp), MSG_NOSIGNAL) < 0) c->broken = 1;
            pthread_mutex_unlock(&c->send_lock);

            c->is_playing = 1;
            printf("[RTSP] Client %s started PLAY on stream %d (%s)!\n", client_ip, sid, (sid == 1) ? "Sub" : "Main");

            send_parameter_sets_to_client(c);
        } else if (strcmp(method, "TEARDOWN") == 0) {
            snprintf(resp, sizeof(resp),
                     "RTSP/1.0 200 OK\r\n"
                     "CSeq: %d\r\n"
                     "Session: %08X\r\n\r\n",
                     cseq, c->session_id);
            pthread_mutex_lock(&c->send_lock);
            send(cfd, resp, strlen(resp), MSG_NOSIGNAL);
            pthread_mutex_unlock(&c->send_lock);
            break;
        } else {
            snprintf(resp, sizeof(resp),
                     "RTSP/1.0 200 OK\r\n"
                     "CSeq: %d\r\n\r\n",
                     cseq);
            pthread_mutex_lock(&c->send_lock);
            if (send(cfd, resp, strlen(resp), MSG_NOSIGNAL) < 0) c->broken = 1;
            pthread_mutex_unlock(&c->send_lock);
        }
        } // End of if (method == OPTIONS) ... else ...
    }

client_done:
    printf("[RTSP] Client disconnected: %s (slot %d)\n", client_ip, client_idx);
    pthread_mutex_lock(&c->send_lock);
    c->active = 0;
    c->is_playing = 0;
    c->authenticated = 0;
    c->broken = 1;
    if (c->udp_rtp_fd > 0) {
        close(c->udp_rtp_fd);
        c->udp_rtp_fd = 0;
    }
    if (c->udp_audio_rtp_fd > 0) {
        close(c->udp_audio_rtp_fd);
        c->udp_audio_rtp_fd = 0;
    }
    c->video_setup = 0;
    c->audio_setup = 0;
    if (c->fd >= 0) {
        close(c->fd);
        c->fd = -1;
    }
    pthread_mutex_unlock(&c->send_lock);

    pthread_mutex_lock(&clients_lock);
    c->in_use = 0;
    pthread_mutex_unlock(&clients_lock);
    return NULL;
}

static const char *SOAP_ENV_HDR =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
    "<SOAP-ENV:Envelope "
    "xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\" "
    "xmlns:SOAP-ENC=\"http://www.w3.org/2003/05/soap-encoding\" "
    "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
    "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
    "xmlns:tt=\"http://www.onvif.org/ver10/schema\" "
    "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" "
    "xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" "
    "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\">\r\n"
    "<SOAP-ENV:Body>";

static const char *SOAP_ENV_FTR = "</SOAP-ENV:Body></SOAP-ENV:Envelope>\r\n";

static const char *SOAP11_ENV_HDR =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n"
    "<SOAP-ENV:Envelope "
    "xmlns:SOAP-ENV=\"http://schemas.xmlsoap.org/soap/envelope/\" "
    "xmlns:SOAP-ENC=\"http://schemas.xmlsoap.org/soap/encoding/\" "
    "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
    "xmlns:xsd=\"http://www.w3.org/2001/XMLSchema\" "
    "xmlns:tt=\"http://www.onvif.org/ver10/schema\" "
    "xmlns:tds=\"http://www.onvif.org/ver10/device/wsdl\" "
    "xmlns:trt=\"http://www.onvif.org/ver10/media/wsdl\" "
    "xmlns:tptz=\"http://www.onvif.org/ver20/ptz/wsdl\">\r\n"
    "<SOAP-ENV:Body>";

static void ptz_get_pos(int *out_x, int *out_y) {
    int cx = 1975, cy = 718;
    FILE *f = fopen("/proc/XmMotor", "r");
    if (f) {
        char line[128];
        int is_y = 0;
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "motor y")) is_y = 1;
            if (strstr(line, "curr step")) {
                char *colon = strchr(line, ':');
                if (colon) {
                    int val = atoi(colon + 1);
                    if (is_y) cy = val;
                    else cx = val;
                }
            }
        }
        fclose(f);
    }
    if (out_x) *out_x = cx;
    if (out_y) *out_y = cy;
}

static void ptz_set_pos(int new_x, int new_y) {
    if (new_x < 0) new_x = 0;
    if (new_x > 3950) new_x = 3950;
    if (new_y < 0) new_y = 0;
    if (new_y > 1436) new_y = 1436;

    FILE *f = fopen("/proc/XmMotor", "w");
    if (f) {
        fprintf(f, "setstep %d 20 %d 20\n", new_x, new_y);
        fclose(f);
    }
    printf("[PTZ] Position set: (%d, %d)\n", new_x, new_y);
}

static int parse_cam_cfg_flag(const char *buf, const char *key) {
    char pat[64];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    const char *p = strstr(buf, pat);
    if (!p) return 0;
    const char *col = strchr(p + strlen(pat), ':');
    if (!col) return 0;
    col++;
    while (*col == ' ' || *col == '\t') col++;
    if (*col == '1' || strncasecmp(col, "true", 4) == 0) return 1;
    return 0;
}

static void ptz_get_invert(int *inv_x, int *inv_y) {
    int mirror = 0, flip = 0, ptz_h = 0, ptz_v = 0;
    FILE *f = fopen("/mnt/mtd/Config/CameraSettings.json", "r");
    if (f) {
        char buf[4096];
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = '\0';
        fclose(f);
        mirror = parse_cam_cfg_flag(buf, "mirror");
        flip = parse_cam_cfg_flag(buf, "flip");
        ptz_h = parse_cam_cfg_flag(buf, "ptz_h_invert");
        ptz_v = parse_cam_cfg_flag(buf, "ptz_v_invert");
    }
    if (inv_x) *inv_x = (mirror ^ ptz_h);
    if (inv_y) *inv_y = (flip ^ ptz_v);
}

static void ptz_move(int pan, int tilt, int step_amount) {
    int inv_x = 0, inv_y = 0;
    ptz_get_invert(&inv_x, &inv_y);
    if (inv_x) pan = -pan;
    if (inv_y) tilt = -tilt;

    int cx = 1975, cy = 718;
    ptz_get_pos(&cx, &cy);
    int new_x = cx + pan * step_amount;
    int new_y = cy + tilt * step_amount;
    ptz_set_pos(new_x, new_y);
    printf("[PTZ] Moved: pan=%d tilt=%d step=%d (inv=%d,%d) -> (%d, %d)\n",
           pan, tilt, step_amount, inv_x, inv_y, new_x, new_y);
}

static int get_coord_attr(const char *buf, char coord, float *out_val) {
    const char *p = buf;
    while ((p = strchr(p, coord)) != NULL) {
        if (p > buf && p[-1] != ' ' && p[-1] != '\t' && p[-1] != '<') {
            p++;
            continue;
        }
        const char *eq = p + 1;
        while (*eq == ' ' || *eq == '\t') eq++;
        if (*eq == '=') {
            const char *val = eq + 1;
            while (*val == ' ' || *val == '\t') val++;
            if (*val == '"' || *val == '\'') val++;
            if (sscanf(val, "%f", out_val) == 1) {
                return 1;
            }
        }
        p++;
    }
    return 0;
}

typedef struct {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
} sha1_ctx_t;

#define SHA1_ROL(v, b) (((v) << (b)) | ((v) >> (32 - (b))))
static void sha1_transform(uint32_t state[5], const uint8_t buffer[64]) {
    uint32_t a, b, c, d, e, w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)buffer[i*4] << 24) | ((uint32_t)buffer[i*4+1] << 16) |
               ((uint32_t)buffer[i*4+2] << 8) | buffer[i*4+3];
    }
    for (int i = 16; i < 80; i++) {
        uint32_t t = w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16];
        w[i] = SHA1_ROL(t, 1);
    }
    a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        uint32_t temp = SHA1_ROL(a, 5) + f + e + k + w[i];
        e = d; d = c; c = SHA1_ROL(b, 30); b = a; a = temp;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

static void sha1_calc(const uint8_t *data, size_t len, uint8_t digest[20]) {
    sha1_ctx_t ctx = { .state = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0}, .count = {0, 0} };
    uint8_t pad[128] = {0};
    size_t i = 0;
    while (len - i >= 64) {
        sha1_transform(ctx.state, data + i);
        i += 64;
    }
    size_t rem = len - i;
    memcpy(pad, data + i, rem);
    pad[rem] = 0x80;
    size_t total_bits = len * 8;
    size_t pad_len = (rem < 56) ? 64 : 128;
    for (int b = 0; b < 8; b++) {
        pad[pad_len - 1 - b] = (uint8_t)((total_bits >> (b * 8)) & 0xFF);
    }
    for (size_t off = 0; off < pad_len; off += 64) sha1_transform(ctx.state, pad + off);
    for (int k = 0; k < 5; k++) {
        digest[k*4] = (ctx.state[k] >> 24) & 0xFF;
        digest[k*4+1] = (ctx.state[k] >> 16) & 0xFF;
        digest[k*4+2] = (ctx.state[k] >> 8) & 0xFF;
        digest[k*4+3] = ctx.state[k] & 0xFF;
    }
}

static int extract_xml_tag_val(const char *xml, const char *tag_suffix, char *out, size_t max_len) {
    char pat[64];
    snprintf(pat, sizeof(pat), "%s", tag_suffix);
    const char *p = xml;
    while ((p = strcasestr(p, pat)) != NULL) {
        if (p > xml && (p[-1] == '<' || p[-1] == ':')) {
            const char *gt = strchr(p, '>');
            if (gt && gt > p && gt[-1] != '/') {
                gt++;
                const char *lt = strchr(gt, '<');
                if (lt) {
                    size_t l = (size_t)(lt - gt);
                    if (l >= max_len) l = max_len - 1;
                    memcpy(out, gt, l);
                    out[l] = '\0';
                    return 1;
                }
            }
        }
        p++;
    }
    return 0;
}

static int check_wsse_password(const char *xml, const char *nonce_b64, const char *created, const char *pass_val, const char *candidate_pass) {
    if (!pass_val || !candidate_pass) return 0;
    if (strcmp(pass_val, candidate_pass) == 0) return 1;
    if (!nonce_b64[0] || !created[0]) return 0;

    // Decode nonce base64
    uint8_t nonce_raw[128] = {0};
    size_t in_len = strlen(nonce_b64), n_len = 0;
    for (size_t i = 0; i < in_len; ) {
        while (i < in_len && isspace((unsigned char)nonce_b64[i])) i++;
        if (i >= in_len) break;
        int a = b64_char_to_val(nonce_b64[i++]);
        int b = (i < in_len) ? b64_char_to_val(nonce_b64[i++]) : -1;
        int c = (i < in_len) ? b64_char_to_val(nonce_b64[i++]) : -1;
        int d = (i < in_len) ? b64_char_to_val(nonce_b64[i++]) : -1;
        if (a < 0 || b < 0) break;
        if (n_len < sizeof(nonce_raw)) nonce_raw[n_len++] = (uint8_t)((a << 2) | (b >> 4));
        if (c >= 0 && n_len < sizeof(nonce_raw)) nonce_raw[n_len++] = (uint8_t)(((b & 0xF) << 4) | (c >> 2));
        if (d >= 0 && n_len < sizeof(nonce_raw)) nonce_raw[n_len++] = (uint8_t)(((c & 0x3) << 6) | d);
    }

    uint8_t concat[512];
    size_t clen = 0;
    if (n_len + strlen(created) + strlen(candidate_pass) >= sizeof(concat)) return 0;
    memcpy(concat + clen, nonce_raw, n_len); clen += n_len;
    memcpy(concat + clen, created, strlen(created)); clen += strlen(created);
    memcpy(concat + clen, candidate_pass, strlen(candidate_pass)); clen += strlen(candidate_pass);

    uint8_t sha[20];
    sha1_calc(concat, clen, sha);
    char expected_b64[64];
    base64_encode(sha, 20, expected_b64);
    (void)xml;
    return (strcmp(pass_val, expected_b64) == 0);
}

static int verify_onvif_auth(const char *req) {
    reload_auth_configs();
    if (!g_stream_auth.enabled) return 1;

    // 1. Check HTTP Authorization: Basic ...
    if (verify_rtsp_auth(req, "") == 1) return 1;

    // 2. Check WS-Security UsernameToken in SOAP XML
    char u[64] = {0}, p[128] = {0}, nonce[128] = {0}, created[64] = {0};
    if (extract_xml_tag_val(req, "Username", u, sizeof(u)) &&
        extract_xml_tag_val(req, "Password", p, sizeof(p))) {
        extract_xml_tag_val(req, "Nonce", nonce, sizeof(nonce));
        extract_xml_tag_val(req, "Created", created, sizeof(created));
        if (strcmp(u, g_stream_auth.username) == 0 && check_wsse_password(req, nonce, created, p, g_stream_auth.password)) {
            return 1;
        }
        if (strcmp(u, g_admin_user) == 0 && check_wsse_password(req, nonce, created, p, g_admin_pass)) {
            return 1;
        }
    }
    return 0;
}

static void *onvif_thread(void *arg) {
    (void)arg;
    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(ONVIF_PORT);
    sin.sin_addr.s_addr = INADDR_ANY;

    if (bind(sfd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        printf("[ONVIF] Bind failed on port 8899: %s\n", strerror(errno));
        close(sfd);
        return NULL;
    }
    listen(sfd, 10);
    printf("[ONVIF] Listening on port 8899...\n");

    while (1) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(sfd, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) continue;

        struct timeval tv_onvif = { .tv_sec = 2, .tv_usec = 0 };
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv_onvif, sizeof(tv_onvif));
        setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv_onvif, sizeof(tv_onvif));

        char buf[8192];
        ssize_t total = 0;
        int content_len = -1;
        while (total < (ssize_t)sizeof(buf) - 1) {
            ssize_t n = recv(cfd, buf + total, sizeof(buf) - 1 - total, 0);
            if (n <= 0) break;
            total += n;
            buf[total] = '\0';

            char *hdr_end = strstr(buf, "\r\n\r\n");
            if (hdr_end) {
                if (content_len < 0) {
                    char *cl = strcasestr(buf, "Content-Length:");
                    if (cl) content_len = atoi(cl + 15);
                    else content_len = -2;
                }
                int body_len = (int)(total - (hdr_end + 4 - buf));
                if (content_len >= 0 && body_len >= content_len) break;
                if (content_len == -2 && strstr(hdr_end + 4, "Envelope>")) break;
            }
        }
        if (total <= 0) { close(cfd); continue; }

        struct sockaddr_in laddr;
        socklen_t llen = sizeof(laddr);
        char host_ip[32];
        strncpy(host_ip, my_ip, sizeof(host_ip));
        if (getsockname(cfd, (struct sockaddr *)&laddr, &llen) == 0) {
            inet_ntop(AF_INET, &laddr.sin_addr, host_ip, sizeof(host_ip));
        }

        int is_soap11 = (strstr(buf, "schemas.xmlsoap.org/soap/envelope") != NULL);
        const char *hdr = is_soap11 ? SOAP11_ENV_HDR : SOAP_ENV_HDR;
        const char *ftr = SOAP_ENV_FTR;
        const char *content_type = is_soap11 ? "text/xml; charset=utf-8" : "application/soap+xml; charset=utf-8";

        static char resp_body[16384];
        resp_body[0] = '\0';

        char action_name[64] = "Unknown";
        char *action_tag = strstr(buf, ":Body>");
        if (!action_tag) action_tag = strstr(buf, "<SOAP-ENV:Body>");
        if (!action_tag) action_tag = strstr(buf, "<s:Body>");
        if (action_tag) {
            char *start = strchr(action_tag, '<');
            if (start) {
                char *colon = strchr(start, ':');
                if (colon && colon < start + 12) start = colon + 1;
                else start++;
                char *end = strpbrk(start, " >\r\n");
                if (end && (end - start < (int)sizeof(action_name))) {
                    strncpy(action_name, start, end - start);
                    action_name[end - start] = '\0';
                }
            }
        }
        printf("[ONVIF] %s -> %s\n", host_ip, action_name);

        // Require authentication on sensitive ONVIF endpoints when stream_auth is enabled
        int needs_auth = (strstr(buf, "GetStreamUri") || strstr(buf, "GetSnapshotUri") ||
                          strstr(buf, "ContinuousMove") || strstr(buf, "RelativeMove") ||
                          strstr(buf, "AbsoluteMove") || strstr(buf, "GotoPreset") ||
                          strstr(buf, "SetPreset") || strstr(buf, "Stop"));
        if (needs_auth && !verify_onvif_auth(buf)) {
            const char *unauth = "HTTP/1.1 401 Unauthorized\r\nWWW-Authenticate: Basic realm=\"CleanCam\"\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            send(cfd, unauth, strlen(unauth), MSG_NOSIGNAL);
            close(cfd);
            continue;
        }

        pthread_mutex_lock(&streams[0].lock);
        const char *enc0 = (streams[0].codec == CODEC_H265) ? "H265" : "H264";
        pthread_mutex_unlock(&streams[0].lock);
        pthread_mutex_lock(&streams[1].lock);
        const char *enc1 = (streams[1].codec == CODEC_H265) ? "H265" : "H264";
        pthread_mutex_unlock(&streams[1].lock);

        if (strstr(buf, "GetDeviceInformation")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<tds:GetDeviceInformationResponse>"
                "<tds:Manufacturer>Xiongmai</tds:Manufacturer>"
                "<tds:Model>GK7201V300</tds:Model>"
                "<tds:FirmwareVersion>CleanCam-1.0</tds:FirmwareVersion>"
                "<tds:SerialNumber>XM000949U5</tds:SerialNumber>"
                "<tds:HardwareId>GK7201V300</tds:HardwareId>"
                "</tds:GetDeviceInformationResponse>%s",
                hdr, ftr);
        } else if (strstr(buf, "GetSystemDateAndTime")) {
            time_t now = time(NULL);
            struct tm tm_buf;
            struct tm *tm_utc = gmtime_r(&now, &tm_buf);
            snprintf(resp_body, sizeof(resp_body),
                "%s<tds:GetSystemDateAndTimeResponse>"
                "<tds:SystemDateAndTime>"
                "<tt:DateTimeType>NTP</tt:DateTimeType>"
                "<tt:DaylightSavings>false</tt:DaylightSavings>"
                "<tt:TimeZone><tt:TZ>GMT</tt:TZ></tt:TimeZone>"
                "<tt:UTCDateTime>"
                "<tt:Time><tt:Hour>%02d</tt:Hour><tt:Minute>%02d</tt:Minute><tt:Second>%02d</tt:Second></tt:Time>"
                "<tt:Date><tt:Year>%04d</tt:Year><tt:Month>%02d</tt:Month><tt:Day>%02d</tt:Day></tt:Date>"
                "</tt:UTCDateTime>"
                "</tds:SystemDateAndTime>"
                "</tds:GetSystemDateAndTimeResponse>%s",
                hdr,
                tm_utc ? tm_utc->tm_hour : 12, tm_utc ? tm_utc->tm_min : 0, tm_utc ? tm_utc->tm_sec : 0,
                tm_utc ? (tm_utc->tm_year + 1900) : 2026, tm_utc ? (tm_utc->tm_mon + 1) : 11, tm_utc ? tm_utc->tm_mday : 17,
                ftr);
        } else if (strstr(buf, "GetCapabilities")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<tds:GetCapabilitiesResponse><tds:Capabilities>"
                "<tt:Device>"
                "<tt:XAddr>http://%s:8899/onvif/device_service</tt:XAddr>"
                "</tt:Device>"
                "<tt:Media>"
                "<tt:XAddr>http://%s:8899/onvif/media_service</tt:XAddr>"
                "<tt:StreamingCapabilities><tt:RTP_RTSP_TCP>true</tt:RTP_RTSP_TCP></tt:StreamingCapabilities>"
                "</tt:Media>"
                "<tt:PTZ>"
                "<tt:XAddr>http://%s:8899/onvif/ptz_service</tt:XAddr>"
                "</tt:PTZ>"
                "</tds:Capabilities></tds:GetCapabilitiesResponse>%s",
                hdr, host_ip, host_ip, host_ip, ftr);
        } else if (strstr(buf, "GetServices")) {
            int inc_caps = (strstr(buf, "IncludeCapability>true") != NULL);
            snprintf(resp_body, sizeof(resp_body),
                "%s<tds:GetServicesResponse>"
                "<tds:Service>"
                "<tds:Namespace>http://www.onvif.org/ver10/device/wsdl</tds:Namespace>"
                "<tds:XAddr>http://%s:8899/onvif/device_service</tds:XAddr>"
                "%s"
                "<tds:Version><tt:Major>2</tt:Major><tt:Minor>60</tt:Minor></tds:Version>"
                "</tds:Service>"
                "<tds:Service>"
                "<tds:Namespace>http://www.onvif.org/ver10/media/wsdl</tds:Namespace>"
                "<tds:XAddr>http://%s:8899/onvif/media_service</tds:XAddr>"
                "%s"
                "<tds:Version><tt:Major>2</tt:Major><tt:Minor>60</tt:Minor></tds:Version>"
                "</tds:Service>"
                "<tds:Service>"
                "<tds:Namespace>http://www.onvif.org/ver20/ptz/wsdl</tds:Namespace>"
                "<tds:XAddr>http://%s:8899/onvif/ptz_service</tds:XAddr>"
                "%s"
                "<tds:Version><tt:Major>2</tt:Major><tt:Minor>60</tt:Minor></tds:Version>"
                "</tds:Service>"
                "</tds:GetServicesResponse>%s",
                hdr,
                host_ip, inc_caps ? "<tds:Capabilities><tds:Network/></tds:Capabilities>" : "",
                host_ip, inc_caps ? "<tds:Capabilities><trt:Capabilities SnapshotUri=\"true\"/></tds:Capabilities>" : "",
                host_ip, inc_caps ? "<tds:Capabilities><tptz:Capabilities MoveStatus=\"true\" StatusPosition=\"true\"/></tds:Capabilities>" : "",
                ftr);
        } else if (strstr(buf, "GetProfiles")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<trt:GetProfilesResponse>"
                "<trt:Profiles token=\"Profile_0\" fixed=\"true\">"
                "<tt:Name>MainStream</tt:Name>"
                "<tt:VideoSourceConfiguration token=\"VSC_0\">"
                "<tt:Name>VideoSourceConfig_0</tt:Name><tt:UseCount>2</tt:UseCount>"
                "<tt:SourceToken>VideoSource_0</tt:SourceToken>"
                "<tt:Bounds x=\"0\" y=\"0\" width=\"1920\" height=\"1080\"/>"
                "</tt:VideoSourceConfiguration>"
                "<tt:VideoEncoderConfiguration token=\"VEC_0\">"
                "<tt:Name>VideoEncoderConfig_0</tt:Name><tt:UseCount>1</tt:UseCount>"
                "<tt:Encoding>%s</tt:Encoding>"
                "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
                "<tt:Quality>5.0</tt:Quality>"
                "<tt:RateControl><tt:FrameRateLimit>12</tt:FrameRateLimit><tt:EncodingInterval>1</tt:EncodingInterval><tt:BitrateLimit>2048</tt:BitrateLimit></tt:RateControl>"
                "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type><tt:IPv4Address>0.0.0.0</tt:IPv4Address></tt:Address><tt:Port>0</tt:Port><tt:TTL>1</tt:TTL><tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
                "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
                "</tt:VideoEncoderConfiguration>"
                "<tt:PTZConfiguration token=\"PTZ_0\">"
                "<tt:Name>PTZConfig_0</tt:Name><tt:UseCount>2</tt:UseCount><tt:NodeToken>Node_0</tt:NodeToken>"
                "<tt:DefaultAbsolutePantTiltPositionSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:DefaultAbsolutePantTiltPositionSpace>"
                "<tt:DefaultAbsoluteZoomPositionSpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:DefaultAbsoluteZoomPositionSpace>"
                "<tt:DefaultRelativePanTiltTranslationSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationGenericSpace</tt:DefaultRelativePanTiltTranslationSpace>"
                "<tt:DefaultRelativeZoomTranslationSpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/TranslationGenericSpace</tt:DefaultRelativeZoomTranslationSpace>"
                "<tt:DefaultContinuousPanTiltVelocitySpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:DefaultContinuousPanTiltVelocitySpace>"
                "<tt:DefaultContinuousZoomVelocitySpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace</tt:DefaultContinuousZoomVelocitySpace>"
                "<tt:DefaultSpeed><tt:PanTilt x=\"1.0\" y=\"1.0\" space=\"http://www.onvif.org/ver10/tptz/PanTiltSpaces/GenericSpeedSpace\"/><tt:Zoom x=\"1.0\" space=\"http://www.onvif.org/ver10/tptz/ZoomSpaces/GenericSpeedSpace\"/></tt:DefaultSpeed>"
                "<tt:DefaultPTZTimeout>PT5S</tt:DefaultPTZTimeout>"
                "<tt:PanTiltLimits><tt:Range><tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:URI><tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange><tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange></tt:Range></tt:PanTiltLimits>"
                "<tt:ZoomLimits><tt:Range><tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:URI><tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange></tt:Range></tt:ZoomLimits>"
                "</tt:PTZConfiguration>"
                "</trt:Profiles>"
                "<trt:Profiles token=\"Profile_1\" fixed=\"true\">"
                "<tt:Name>SubStream</tt:Name>"
                "<tt:VideoSourceConfiguration token=\"VSC_1\">"
                "<tt:Name>VideoSourceConfig_1</tt:Name><tt:UseCount>2</tt:UseCount>"
                "<tt:SourceToken>VideoSource_0</tt:SourceToken>"
                "<tt:Bounds x=\"0\" y=\"0\" width=\"800\" height=\"448\"/>"
                "</tt:VideoSourceConfiguration>"
                "<tt:VideoEncoderConfiguration token=\"VEC_1\">"
                "<tt:Name>VideoEncoderConfig_1</tt:Name><tt:UseCount>1</tt:UseCount>"
                "<tt:Encoding>%s</tt:Encoding>"
                "<tt:Resolution><tt:Width>800</tt:Width><tt:Height>448</tt:Height></tt:Resolution>"
                "<tt:Quality>5.0</tt:Quality>"
                "<tt:RateControl><tt:FrameRateLimit>12</tt:FrameRateLimit><tt:EncodingInterval>1</tt:EncodingInterval><tt:BitrateLimit>1024</tt:BitrateLimit></tt:RateControl>"
                "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type><tt:IPv4Address>0.0.0.0</tt:IPv4Address></tt:Address><tt:Port>0</tt:Port><tt:TTL>1</tt:TTL><tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
                "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
                "</tt:VideoEncoderConfiguration>"
                "<tt:PTZConfiguration token=\"PTZ_1\">"
                "<tt:Name>PTZConfig_1</tt:Name><tt:UseCount>2</tt:UseCount><tt:NodeToken>Node_0</tt:NodeToken>"
                "<tt:DefaultAbsolutePantTiltPositionSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:DefaultAbsolutePantTiltPositionSpace>"
                "<tt:DefaultAbsoluteZoomPositionSpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:DefaultAbsoluteZoomPositionSpace>"
                "<tt:DefaultRelativePanTiltTranslationSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationGenericSpace</tt:DefaultRelativePanTiltTranslationSpace>"
                "<tt:DefaultRelativeZoomTranslationSpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/TranslationGenericSpace</tt:DefaultRelativeZoomTranslationSpace>"
                "<tt:DefaultContinuousPanTiltVelocitySpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:DefaultContinuousPanTiltVelocitySpace>"
                "<tt:DefaultContinuousZoomVelocitySpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace</tt:DefaultContinuousZoomVelocitySpace>"
                "<tt:DefaultSpeed><tt:PanTilt x=\"1.0\" y=\"1.0\" space=\"http://www.onvif.org/ver10/tptz/PanTiltSpaces/GenericSpeedSpace\"/><tt:Zoom x=\"1.0\" space=\"http://www.onvif.org/ver10/tptz/ZoomSpaces/GenericSpeedSpace\"/></tt:DefaultSpeed>"
                "<tt:DefaultPTZTimeout>PT5S</tt:DefaultPTZTimeout>"
                "<tt:PanTiltLimits><tt:Range><tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:URI><tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange><tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange></tt:Range></tt:PanTiltLimits>"
                "<tt:ZoomLimits><tt:Range><tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:URI><tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange></tt:Range></tt:ZoomLimits>"
                "</tt:PTZConfiguration>"
                "</trt:Profiles>"
                "</trt:GetProfilesResponse>%s",
                hdr, enc0, enc1, ftr);
        } else if (strstr(buf, "GetProfile")) {
            int req_sub = (strstr(buf, "Profile_1") != NULL || strcasestr(buf, "sub") != NULL);
            if (req_sub) {
                snprintf(resp_body, sizeof(resp_body),
                    "%s<trt:GetProfileResponse>"
                    "<trt:Profile token=\"Profile_1\" fixed=\"true\">"
                    "<tt:Name>SubStream</tt:Name>"
                    "<tt:VideoSourceConfiguration token=\"VSC_1\">"
                    "<tt:Name>VideoSourceConfig_1</tt:Name><tt:UseCount>2</tt:UseCount>"
                    "<tt:SourceToken>VideoSource_0</tt:SourceToken>"
                    "<tt:Bounds x=\"0\" y=\"0\" width=\"800\" height=\"448\"/>"
                    "</tt:VideoSourceConfiguration>"
                    "<tt:VideoEncoderConfiguration token=\"VEC_1\">"
                    "<tt:Name>VideoEncoderConfig_1</tt:Name><tt:UseCount>1</tt:UseCount>"
                    "<tt:Encoding>%s</tt:Encoding>"
                    "<tt:Resolution><tt:Width>800</tt:Width><tt:Height>448</tt:Height></tt:Resolution>"
                    "<tt:Quality>5.0</tt:Quality>"
                    "<tt:RateControl><tt:FrameRateLimit>12</tt:FrameRateLimit><tt:EncodingInterval>1</tt:EncodingInterval><tt:BitrateLimit>1024</tt:BitrateLimit></tt:RateControl>"
                    "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type><tt:IPv4Address>0.0.0.0</tt:IPv4Address></tt:Address><tt:Port>0</tt:Port><tt:TTL>1</tt:TTL><tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
                    "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
                    "</tt:VideoEncoderConfiguration>"
                    "<tt:PTZConfiguration token=\"PTZ_1\">"
                    "<tt:Name>PTZConfig_1</tt:Name><tt:UseCount>2</tt:UseCount><tt:NodeToken>Node_0</tt:NodeToken>"
                    "<tt:DefaultAbsolutePantTiltPositionSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:DefaultAbsolutePantTiltPositionSpace>"
                    "<tt:DefaultAbsoluteZoomPositionSpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:DefaultAbsoluteZoomPositionSpace>"
                    "<tt:DefaultRelativePanTiltTranslationSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationGenericSpace</tt:DefaultRelativePanTiltTranslationSpace>"
                    "<tt:DefaultRelativeZoomTranslationSpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/TranslationGenericSpace</tt:DefaultRelativeZoomTranslationSpace>"
                    "<tt:DefaultContinuousPanTiltVelocitySpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:DefaultContinuousPanTiltVelocitySpace>"
                    "<tt:DefaultContinuousZoomVelocitySpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace</tt:DefaultContinuousZoomVelocitySpace>"
                    "<tt:DefaultSpeed><tt:PanTilt x=\"1.0\" y=\"1.0\" space=\"http://www.onvif.org/ver10/tptz/PanTiltSpaces/GenericSpeedSpace\"/><tt:Zoom x=\"1.0\" space=\"http://www.onvif.org/ver10/tptz/ZoomSpaces/GenericSpeedSpace\"/></tt:DefaultSpeed>"
                    "<tt:DefaultPTZTimeout>PT5S</tt:DefaultPTZTimeout>"
                    "<tt:PanTiltLimits><tt:Range><tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:URI><tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange><tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange></tt:Range></tt:PanTiltLimits>"
                    "<tt:ZoomLimits><tt:Range><tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:URI><tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange></tt:Range></tt:ZoomLimits>"
                    "</tt:PTZConfiguration>"
                    "</trt:Profile>"
                    "</trt:GetProfileResponse>%s",
                    hdr, enc1, ftr);
            } else {
                snprintf(resp_body, sizeof(resp_body),
                    "%s<trt:GetProfileResponse>"
                    "<trt:Profile token=\"Profile_0\" fixed=\"true\">"
                    "<tt:Name>MainStream</tt:Name>"
                    "<tt:VideoSourceConfiguration token=\"VSC_0\">"
                    "<tt:Name>VideoSourceConfig_0</tt:Name><tt:UseCount>2</tt:UseCount>"
                    "<tt:SourceToken>VideoSource_0</tt:SourceToken>"
                    "<tt:Bounds x=\"0\" y=\"0\" width=\"1920\" height=\"1080\"/>"
                    "</tt:VideoSourceConfiguration>"
                    "<tt:VideoEncoderConfiguration token=\"VEC_0\">"
                    "<tt:Name>VideoEncoderConfig_0</tt:Name><tt:UseCount>1</tt:UseCount>"
                    "<tt:Encoding>%s</tt:Encoding>"
                    "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
                    "<tt:Quality>5.0</tt:Quality>"
                    "<tt:RateControl><tt:FrameRateLimit>12</tt:FrameRateLimit><tt:EncodingInterval>1</tt:EncodingInterval><tt:BitrateLimit>2048</tt:BitrateLimit></tt:RateControl>"
                    "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type><tt:IPv4Address>0.0.0.0</tt:IPv4Address></tt:Address><tt:Port>0</tt:Port><tt:TTL>1</tt:TTL><tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
                    "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
                    "</tt:VideoEncoderConfiguration>"
                    "<tt:PTZConfiguration token=\"PTZ_0\">"
                    "<tt:Name>PTZConfig_0</tt:Name><tt:UseCount>2</tt:UseCount><tt:NodeToken>Node_0</tt:NodeToken>"
                    "<tt:DefaultAbsolutePantTiltPositionSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:DefaultAbsolutePantTiltPositionSpace>"
                    "<tt:DefaultAbsoluteZoomPositionSpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:DefaultAbsoluteZoomPositionSpace>"
                    "<tt:DefaultRelativePanTiltTranslationSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationGenericSpace</tt:DefaultRelativePanTiltTranslationSpace>"
                    "<tt:DefaultRelativeZoomTranslationSpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/TranslationGenericSpace</tt:DefaultRelativeZoomTranslationSpace>"
                    "<tt:DefaultContinuousPanTiltVelocitySpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:DefaultContinuousPanTiltVelocitySpace>"
                    "<tt:DefaultContinuousZoomVelocitySpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace</tt:DefaultContinuousZoomVelocitySpace>"
                    "<tt:DefaultSpeed><tt:PanTilt x=\"1.0\" y=\"1.0\" space=\"http://www.onvif.org/ver10/tptz/PanTiltSpaces/GenericSpeedSpace\"/><tt:Zoom x=\"1.0\" space=\"http://www.onvif.org/ver10/tptz/ZoomSpaces/GenericSpeedSpace\"/></tt:DefaultSpeed>"
                    "<tt:DefaultPTZTimeout>PT5S</tt:DefaultPTZTimeout>"
                    "<tt:PanTiltLimits><tt:Range><tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:URI><tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange><tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange></tt:Range></tt:PanTiltLimits>"
                    "<tt:ZoomLimits><tt:Range><tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:URI><tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange></tt:Range></tt:ZoomLimits>"
                    "</tt:PTZConfiguration>"
                    "</trt:Profile>"
                    "</trt:GetProfileResponse>%s",
                    hdr, enc0, ftr);
            }
        } else if (strstr(buf, "GetStreamUri")) {
            int req_sub = (strstr(buf, "Profile_1") != NULL || strcasestr(buf, "sub") != NULL || strstr(buf, "ch1") != NULL || strstr(buf, "stream1") != NULL);
            const char *stream_path = req_sub ? "live/ch1" : "live/ch0";

            snprintf(resp_body, sizeof(resp_body),
                "%s<trt:GetStreamUriResponse><trt:MediaUri>"
                "<tt:Uri>rtsp://%s:554/%s</tt:Uri>"
                "<tt:InvalidAfterConnect>false</tt:InvalidAfterConnect>"
                "<tt:InvalidAfterReboot>false</tt:InvalidAfterReboot>"
                "<tt:Timeout>PT30S</tt:Timeout>"
                "</trt:MediaUri></trt:GetStreamUriResponse>%s",
                hdr, host_ip, stream_path, ftr);
        } else if (strstr(buf, "GetSnapshotUri")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<trt:GetSnapshotUriResponse><trt:MediaUri>"
                "<tt:Uri>http://%s/cgi-bin/snapshot</tt:Uri>"
                "<tt:InvalidAfterConnect>false</tt:InvalidAfterConnect>"
                "<tt:InvalidAfterReboot>false</tt:InvalidAfterReboot>"
                "<tt:Timeout>PT30S</tt:Timeout>"
                "</trt:MediaUri></trt:GetSnapshotUriResponse>%s",
                hdr, host_ip, ftr);
        } else if (strstr(buf, "GetVideoSources")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<trt:GetVideoSourcesResponse>"
                "<trt:VideoSources token=\"VideoSource_0\">"
                "<tt:Framerate>12.0</tt:Framerate>"
                "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
                "</trt:VideoSources>"
                "</trt:GetVideoSourcesResponse>%s",
                hdr, ftr);
        } else if (strstr(buf, "GetVideoSourceConfigurations")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<trt:GetVideoSourceConfigurationsResponse>"
                "<trt:Configurations token=\"VSC_0\">"
                "<tt:Name>VideoSourceConfig_0</tt:Name><tt:UseCount>2</tt:UseCount>"
                "<tt:SourceToken>VideoSource_0</tt:SourceToken>"
                "<tt:Bounds x=\"0\" y=\"0\" width=\"1920\" height=\"1080\"/>"
                "</trt:Configurations>"
                "</trt:GetVideoSourceConfigurationsResponse>%s",
                hdr, ftr);
        } else if (strstr(buf, "GetVideoSourceConfiguration")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<trt:GetVideoSourceConfigurationResponse>"
                "<trt:Configuration token=\"VSC_0\">"
                "<tt:Name>VideoSourceConfig_0</tt:Name><tt:UseCount>2</tt:UseCount>"
                "<tt:SourceToken>VideoSource_0</tt:SourceToken>"
                "<tt:Bounds x=\"0\" y=\"0\" width=\"1920\" height=\"1080\"/>"
                "</trt:Configuration>"
                "</trt:GetVideoSourceConfigurationResponse>%s",
                hdr, ftr);
        } else if (strstr(buf, "GetVideoEncoderConfigurations")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<trt:GetVideoEncoderConfigurationsResponse>"
                "<trt:Configurations token=\"VEC_0\">"
                "<tt:Name>VideoEncoderConfig_0</tt:Name><tt:UseCount>1</tt:UseCount>"
                "<tt:Encoding>%s</tt:Encoding>"
                "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
                "<tt:Quality>5.0</tt:Quality>"
                "<tt:RateControl><tt:FrameRateLimit>12</tt:FrameRateLimit><tt:EncodingInterval>1</tt:EncodingInterval><tt:BitrateLimit>2048</tt:BitrateLimit></tt:RateControl>"
                "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type><tt:IPv4Address>0.0.0.0</tt:IPv4Address></tt:Address><tt:Port>0</tt:Port><tt:TTL>1</tt:TTL><tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
                "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
                "</trt:Configurations>"
                "<trt:Configurations token=\"VEC_1\">"
                "<tt:Name>VideoEncoderConfig_1</tt:Name><tt:UseCount>1</tt:UseCount>"
                "<tt:Encoding>%s</tt:Encoding>"
                "<tt:Resolution><tt:Width>800</tt:Width><tt:Height>448</tt:Height></tt:Resolution>"
                "<tt:Quality>5.0</tt:Quality>"
                "<tt:RateControl><tt:FrameRateLimit>12</tt:FrameRateLimit><tt:EncodingInterval>1</tt:EncodingInterval><tt:BitrateLimit>1024</tt:BitrateLimit></tt:RateControl>"
                "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type><tt:IPv4Address>0.0.0.0</tt:IPv4Address></tt:Address><tt:Port>0</tt:Port><tt:TTL>1</tt:TTL><tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
                "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
                "</trt:Configurations>"
                "</trt:GetVideoEncoderConfigurationsResponse>%s",
                hdr, enc0, enc1, ftr);
        } else if (strstr(buf, "GetVideoEncoderConfigurationOptions")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<trt:GetVideoEncoderConfigurationOptionsResponse>"
                "<trt:Options>"
                "<tt:QualityRange><tt:Min>1</tt:Min><tt:Max>6</tt:Max></tt:QualityRange>"
                "<tt:H264>"
                "<tt:ResolutionsAvailable><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:ResolutionsAvailable>"
                "<tt:ResolutionsAvailable><tt:Width>800</tt:Width><tt:Height>448</tt:Height></tt:ResolutionsAvailable>"
                "<tt:GovLengthRange><tt:Min>1</tt:Min><tt:Max>60</tt:Max></tt:GovLengthRange>"
                "<tt:FrameRateRange><tt:Min>1</tt:Min><tt:Max>25</tt:Max></tt:FrameRateRange>"
                "<tt:EncodingIntervalRange><tt:Min>1</tt:Min><tt:Max>1</tt:Max></tt:EncodingIntervalRange>"
                "<tt:H264ProfilesSupported>Main</tt:H264ProfilesSupported>"
                "</tt:H264>"
                "</trt:Options>"
                "</trt:GetVideoEncoderConfigurationOptionsResponse>%s",
                hdr, ftr);
        } else if (strstr(buf, "GetVideoEncoderConfiguration")) {
            int req_sub = (strstr(buf, "VEC_1") != NULL || strstr(buf, "Profile_1") != NULL || strcasestr(buf, "sub") != NULL);
            if (req_sub) {
                snprintf(resp_body, sizeof(resp_body),
                    "%s<trt:GetVideoEncoderConfigurationResponse>"
                    "<trt:Configuration token=\"VEC_1\">"
                    "<tt:Name>VideoEncoderConfig_1</tt:Name><tt:UseCount>1</tt:UseCount>"
                    "<tt:Encoding>%s</tt:Encoding>"
                    "<tt:Resolution><tt:Width>800</tt:Width><tt:Height>448</tt:Height></tt:Resolution>"
                    "<tt:Quality>5.0</tt:Quality>"
                    "<tt:RateControl><tt:FrameRateLimit>12</tt:FrameRateLimit><tt:EncodingInterval>1</tt:EncodingInterval><tt:BitrateLimit>1024</tt:BitrateLimit></tt:RateControl>"
                    "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type><tt:IPv4Address>0.0.0.0</tt:IPv4Address></tt:Address><tt:Port>0</tt:Port><tt:TTL>1</tt:TTL><tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
                    "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
                    "</trt:Configuration>"
                    "</trt:GetVideoEncoderConfigurationResponse>%s",
                    hdr, enc1, ftr);
            } else {
                snprintf(resp_body, sizeof(resp_body),
                    "%s<trt:GetVideoEncoderConfigurationResponse>"
                    "<trt:Configuration token=\"VEC_0\">"
                    "<tt:Name>VideoEncoderConfig_0</tt:Name><tt:UseCount>1</tt:UseCount>"
                    "<tt:Encoding>%s</tt:Encoding>"
                    "<tt:Resolution><tt:Width>1920</tt:Width><tt:Height>1080</tt:Height></tt:Resolution>"
                    "<tt:Quality>5.0</tt:Quality>"
                    "<tt:RateControl><tt:FrameRateLimit>12</tt:FrameRateLimit><tt:EncodingInterval>1</tt:EncodingInterval><tt:BitrateLimit>2048</tt:BitrateLimit></tt:RateControl>"
                    "<tt:Multicast><tt:Address><tt:Type>IPv4</tt:Type><tt:IPv4Address>0.0.0.0</tt:IPv4Address></tt:Address><tt:Port>0</tt:Port><tt:TTL>1</tt:TTL><tt:AutoStart>false</tt:AutoStart></tt:Multicast>"
                    "<tt:SessionTimeout>PT60S</tt:SessionTimeout>"
                    "</trt:Configuration>"
                    "</trt:GetVideoEncoderConfigurationResponse>%s",
                    hdr, enc0, ftr);
            }
        } else if (strstr(buf, "GetNodes")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<tptz:GetNodesResponse>"
                "<tptz:PTZNode token=\"Node_0\" FixedHomePosition=\"false\">"
                "<tt:Name>Node_0</tt:Name>"
                "<tt:SupportedPTZSpaces>"
                "<tt:AbsolutePanTiltPositionSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "<tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange>"
                "</tt:AbsolutePanTiltPositionSpace>"
                "<tt:AbsoluteZoomPositionSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "</tt:AbsoluteZoomPositionSpace>"
                "<tt:RelativePanTiltTranslationSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "<tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange>"
                "</tt:RelativePanTiltTranslationSpace>"
                "<tt:RelativePanTiltTranslationSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationSpaceFov</tt:URI>"
                "<tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "<tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange>"
                "</tt:RelativePanTiltTranslationSpace>"
                "<tt:RelativeZoomTranslationSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/TranslationGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "</tt:RelativeZoomTranslationSpace>"
                "<tt:ContinuousPanTiltVelocitySpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "<tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange>"
                "</tt:ContinuousPanTiltVelocitySpace>"
                "<tt:ContinuousZoomVelocitySpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "</tt:ContinuousZoomVelocitySpace>"
                "<tt:PanTiltSpeedSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/GenericSpeedSpace</tt:URI>"
                "<tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "</tt:PanTiltSpeedSpace>"
                "<tt:ZoomSpeedSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/GenericSpeedSpace</tt:URI>"
                "<tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "</tt:ZoomSpeedSpace>"
                "</tt:SupportedPTZSpaces>"
                "<tt:MaximumNumberOfPresets>16</tt:MaximumNumberOfPresets>"
                "<tt:HomeSupported>false</tt:HomeSupported>"
                "</tptz:PTZNode>"
                "</tptz:GetNodesResponse>%s",
                hdr, ftr);
        } else if (strstr(buf, "GetNode")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<tptz:GetNodeResponse>"
                "<tptz:PTZNode token=\"Node_0\" FixedHomePosition=\"false\">"
                "<tt:Name>Node_0</tt:Name>"
                "<tt:SupportedPTZSpaces>"
                "<tt:AbsolutePanTiltPositionSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "<tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange>"
                "</tt:AbsolutePanTiltPositionSpace>"
                "<tt:AbsoluteZoomPositionSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "</tt:AbsoluteZoomPositionSpace>"
                "<tt:RelativePanTiltTranslationSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "<tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange>"
                "</tt:RelativePanTiltTranslationSpace>"
                "<tt:RelativePanTiltTranslationSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationSpaceFov</tt:URI>"
                "<tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "<tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange>"
                "</tt:RelativePanTiltTranslationSpace>"
                "<tt:RelativeZoomTranslationSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/TranslationGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "</tt:RelativeZoomTranslationSpace>"
                "<tt:ContinuousPanTiltVelocitySpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "<tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange>"
                "</tt:ContinuousPanTiltVelocitySpace>"
                "<tt:ContinuousZoomVelocitySpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "</tt:ContinuousZoomVelocitySpace>"
                "<tt:PanTiltSpeedSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/GenericSpeedSpace</tt:URI>"
                "<tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "</tt:PanTiltSpeedSpace>"
                "<tt:ZoomSpeedSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/GenericSpeedSpace</tt:URI>"
                "<tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "</tt:ZoomSpeedSpace>"
                "</tt:SupportedPTZSpaces>"
                "<tt:MaximumNumberOfPresets>16</tt:MaximumNumberOfPresets>"
                "<tt:HomeSupported>false</tt:HomeSupported>"
                "</tptz:PTZNode>"
                "</tptz:GetNodeResponse>%s",
                hdr, ftr);
        } else if (strstr(buf, "GetConfigurationOptions")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<tptz:GetConfigurationOptionsResponse>"
                "<tptz:PTZConfigurationOptions>"
                "<tt:Spaces>"
                "<tt:AbsolutePanTiltPositionSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "<tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange>"
                "</tt:AbsolutePanTiltPositionSpace>"
                "<tt:AbsoluteZoomPositionSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "</tt:AbsoluteZoomPositionSpace>"
                "<tt:RelativePanTiltTranslationSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "<tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange>"
                "</tt:RelativePanTiltTranslationSpace>"
                "<tt:RelativePanTiltTranslationSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationSpaceFov</tt:URI>"
                "<tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "<tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange>"
                "</tt:RelativePanTiltTranslationSpace>"
                "<tt:RelativeZoomTranslationSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/TranslationGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "</tt:RelativeZoomTranslationSpace>"
                "<tt:ContinuousPanTiltVelocitySpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "<tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange>"
                "</tt:ContinuousPanTiltVelocitySpace>"
                "<tt:ContinuousZoomVelocitySpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace</tt:URI>"
                "<tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "</tt:ContinuousZoomVelocitySpace>"
                "<tt:PanTiltSpeedSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/GenericSpeedSpace</tt:URI>"
                "<tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "</tt:PanTiltSpeedSpace>"
                "<tt:ZoomSpeedSpace>"
                "<tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/GenericSpeedSpace</tt:URI>"
                "<tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange>"
                "</tt:ZoomSpeedSpace>"
                "</tt:Spaces>"
                "<tt:PTZTimeout><tt:Min>PT1S</tt:Min><tt:Max>PT10S</tt:Max></tt:PTZTimeout>"
                "</tptz:PTZConfigurationOptions>"
                "</tptz:GetConfigurationOptionsResponse>%s",
                hdr, ftr);
        } else if (strstr(buf, "GetConfigurations")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<tptz:GetConfigurationsResponse>"
                "<tptz:PTZConfiguration token=\"PTZ_0\">"
                "<tt:Name>PTZConfig_0</tt:Name><tt:UseCount>2</tt:UseCount><tt:NodeToken>Node_0</tt:NodeToken>"
                "<tt:DefaultAbsolutePantTiltPositionSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:DefaultAbsolutePantTiltPositionSpace>"
                "<tt:DefaultAbsoluteZoomPositionSpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:DefaultAbsoluteZoomPositionSpace>"
                "<tt:DefaultRelativePanTiltTranslationSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationGenericSpace</tt:DefaultRelativePanTiltTranslationSpace>"
                "<tt:DefaultRelativeZoomTranslationSpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/TranslationGenericSpace</tt:DefaultRelativeZoomTranslationSpace>"
                "<tt:DefaultContinuousPanTiltVelocitySpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:DefaultContinuousPanTiltVelocitySpace>"
                "<tt:DefaultContinuousZoomVelocitySpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace</tt:DefaultContinuousZoomVelocitySpace>"
                "<tt:DefaultSpeed><tt:PanTilt x=\"1.0\" y=\"1.0\" space=\"http://www.onvif.org/ver10/tptz/PanTiltSpaces/GenericSpeedSpace\"/><tt:Zoom x=\"1.0\" space=\"http://www.onvif.org/ver10/tptz/ZoomSpaces/GenericSpeedSpace\"/></tt:DefaultSpeed>"
                "<tt:DefaultPTZTimeout>PT5S</tt:DefaultPTZTimeout>"
                "<tt:PanTiltLimits><tt:Range><tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:URI><tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange><tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange></tt:Range></tt:PanTiltLimits>"
                "<tt:ZoomLimits><tt:Range><tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:URI><tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange></tt:Range></tt:ZoomLimits>"
                "</tptz:PTZConfiguration>"
                "</tptz:GetConfigurationsResponse>%s",
                hdr, ftr);
        } else if (strstr(buf, "GetConfiguration")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<tptz:GetConfigurationResponse>"
                "<tptz:PTZConfiguration token=\"PTZ_0\">"
                "<tt:Name>PTZConfig_0</tt:Name><tt:UseCount>2</tt:UseCount><tt:NodeToken>Node_0</tt:NodeToken>"
                "<tt:DefaultAbsolutePantTiltPositionSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:DefaultAbsolutePantTiltPositionSpace>"
                "<tt:DefaultAbsoluteZoomPositionSpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:DefaultAbsoluteZoomPositionSpace>"
                "<tt:DefaultRelativePanTiltTranslationSpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/TranslationGenericSpace</tt:DefaultRelativePanTiltTranslationSpace>"
                "<tt:DefaultRelativeZoomTranslationSpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/TranslationGenericSpace</tt:DefaultRelativeZoomTranslationSpace>"
                "<tt:DefaultContinuousPanTiltVelocitySpace>http://www.onvif.org/ver10/tptz/PanTiltSpaces/VelocityGenericSpace</tt:DefaultContinuousPanTiltVelocitySpace>"
                "<tt:DefaultContinuousZoomVelocitySpace>http://www.onvif.org/ver10/tptz/ZoomSpaces/VelocityGenericSpace</tt:DefaultContinuousZoomVelocitySpace>"
                "<tt:DefaultSpeed><tt:PanTilt x=\"1.0\" y=\"1.0\" space=\"http://www.onvif.org/ver10/tptz/PanTiltSpaces/GenericSpeedSpace\"/><tt:Zoom x=\"1.0\" space=\"http://www.onvif.org/ver10/tptz/ZoomSpaces/GenericSpeedSpace\"/></tt:DefaultSpeed>"
                "<tt:DefaultPTZTimeout>PT5S</tt:DefaultPTZTimeout>"
                "<tt:PanTiltLimits><tt:Range><tt:URI>http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace</tt:URI><tt:XRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange><tt:YRange><tt:Min>-1.0</tt:Min><tt:Max>1.0</tt:Max></tt:YRange></tt:Range></tt:PanTiltLimits>"
                "<tt:ZoomLimits><tt:Range><tt:URI>http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace</tt:URI><tt:XRange><tt:Min>0.0</tt:Min><tt:Max>1.0</tt:Max></tt:XRange></tt:Range></tt:ZoomLimits>"
                "</tptz:PTZConfiguration>"
                "</tptz:GetConfigurationResponse>%s",
                hdr, ftr);
        } else if (strstr(buf, "GetStatus")) {
            int cx = 1975, cy = 718;
            ptz_get_pos(&cx, &cy);
            int inv_x = 0, inv_y = 0;
            ptz_get_invert(&inv_x, &inv_y);
            float fx = (float)(cx - 1975) / 1975.0f;
            float fy = (float)(cy - 718) / 718.0f;
            if (inv_x) fx = -fx;
            if (inv_y) fy = -fy;
            time_t now = time(NULL);
            struct tm tm_buf;
            struct tm *tm_utc = gmtime_r(&now, &tm_buf);
            char tstr[32] = "2026-11-17T12:00:00Z";
            if (tm_utc) strftime(tstr, sizeof(tstr), "%Y-%m-%dT%H:%M:%SZ", tm_utc);
            snprintf(resp_body, sizeof(resp_body),
                "%s<tptz:GetStatusResponse>"
                "<tptz:PTZStatus>"
                "<tt:Position>"
                "<tt:PanTilt x=\"%.2f\" y=\"%.2f\" space=\"http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace\"/>"
                "<tt:Zoom x=\"0.0\" space=\"http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace\"/>"
                "</tt:Position>"
                "<tt:MoveStatus><tt:PanTilt>IDLE</tt:PanTilt><tt:Zoom>IDLE</tt:Zoom></tt:MoveStatus>"
                "<tt:UtcTime>%s</tt:UtcTime>"
                "</tptz:PTZStatus>"
                "</tptz:GetStatusResponse>%s",
                hdr, fx, fy, tstr, ftr);
        } else if (strstr(buf, "ContinuousMove")) {
            float fx = 0, fy = 0;
            get_coord_attr(buf, 'x', &fx);
            get_coord_attr(buf, 'y', &fy);
            int pan = 0, tilt = 0;
            if (fx > 0.05f) pan = 1;
            else if (fx < -0.05f) pan = -1;
            if (fy > 0.05f) tilt = 1;
            else if (fy < -0.05f) tilt = -1;

            if (pan != 0 || tilt != 0) {
                ptz_move(pan, tilt, 120);
            }
            snprintf(resp_body, sizeof(resp_body),
                "%s<tptz:ContinuousMoveResponse/>%s",
                hdr, ftr);
        } else if (strstr(buf, "RelativeMove")) {
            float fx = 0, fy = 0;
            get_coord_attr(buf, 'x', &fx);
            get_coord_attr(buf, 'y', &fy);
            int pan = 0, tilt = 0;
            if (fx > 0.05f) pan = 1;
            else if (fx < -0.05f) pan = -1;
            if (fy > 0.05f) tilt = 1;
            else if (fy < -0.05f) tilt = -1;

            if (pan != 0 || tilt != 0) {
                int step = (int)(fabsf(fx) > fabsf(fy) ? fabsf(fx) * 200 : fabsf(fy) * 200);
                if (step < 60) step = 120;
                ptz_move(pan, tilt, step);
            }
            snprintf(resp_body, sizeof(resp_body),
                "%s<tptz:RelativeMoveResponse/>%s",
                hdr, ftr);
        } else if (strstr(buf, "AbsoluteMove")) {
            float fx = 0, fy = 0;
            if (get_coord_attr(buf, 'x', &fx) && get_coord_attr(buf, 'y', &fy)) {
                int inv_x = 0, inv_y = 0;
                ptz_get_invert(&inv_x, &inv_y);
                if (inv_x) fx = -fx;
                if (inv_y) fy = -fy;
                int target_x = 1975 + (int)(fx * 1975.0f);
                int target_y = 718 + (int)(fy * 718.0f);
                ptz_set_pos(target_x, target_y);
            }
            snprintf(resp_body, sizeof(resp_body),
                "%s<tptz:AbsoluteMoveResponse/>%s",
                hdr, ftr);
        } else if (strstr(buf, "Stop")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<tptz:StopResponse/>%s",
                hdr, ftr);
        } else if (strstr(buf, "GetPresets")) {
            char presets_xml[8192] = {0};
            size_t poff = 0;
            for (int pi = 1; pi <= 8; pi++) {
                char ppath[64];
                snprintf(ppath, sizeof(ppath), "/mnt/mtd/Config/preset_%d.txt", pi);
                FILE *pf = fopen(ppath, "r");
                int px = 1975, py = 718;
                int found = 0;
                if (pf) {
                    if (fscanf(pf, "%d %d", &px, &py) == 2) found = 1;
                    fclose(pf);
                }
                if (found || pi == 1) {
                    int inv_x = 0, inv_y = 0;
                    ptz_get_invert(&inv_x, &inv_y);
                    float fx = (float)(px - 1975) / 1975.0f;
                    float fy = (float)(py - 718) / 718.0f;
                    if (inv_x) fx = -fx;
                    if (inv_y) fy = -fy;
                    int added = snprintf(presets_xml + poff, sizeof(presets_xml) - poff,
                        "<tptz:Preset token=\"%d\"><tt:Name>Preset %d</tt:Name>"
                        "<tt:PTZPosition><tt:PanTilt x=\"%.2f\" y=\"%.2f\" space=\"http://www.onvif.org/ver10/tptz/PanTiltSpaces/PositionGenericSpace\"/>"
                        "<tt:Zoom x=\"0.0\" space=\"http://www.onvif.org/ver10/tptz/ZoomSpaces/PositionGenericSpace\"/></tt:PTZPosition></tptz:Preset>",
                        pi, pi, fx, fy);
                    if (added > 0 && poff + added < sizeof(presets_xml)) poff += added;
                }
            }
            snprintf(resp_body, sizeof(resp_body),
                "%s<tptz:GetPresetsResponse>%s</tptz:GetPresetsResponse>%s",
                hdr, presets_xml, ftr);
        } else if (strstr(buf, "GotoPreset")) {
            int preset = 1;
            char *p = strstr(buf, "PresetToken>");
            if (!p) p = strstr(buf, "preset");
            if (p) {
                while (*p && (*p < '0' || *p > '9')) p++;
                if (*p) preset = atoi(p);
            }
            if (preset < 1 || preset > 16) preset = 1;
            char ppath[64];
            snprintf(ppath, sizeof(ppath), "/mnt/mtd/Config/preset_%d.txt", preset);
            FILE *pf = fopen(ppath, "r");
            if (pf) {
                int px = 1975, py = 718;
                if (fscanf(pf, "%d %d", &px, &py) == 2) ptz_set_pos(px, py);
                fclose(pf);
            } else if (preset == 1) {
                ptz_set_pos(1975, 718);
            }
            snprintf(resp_body, sizeof(resp_body),
                "%s<tptz:GotoPresetResponse/>%s",
                hdr, ftr);
        } else if (strstr(buf, "SetPreset")) {
            int pnum = 1;
            char ptoken[32] = "1";
            char *p = strstr(buf, "PresetToken>");
            if (p) {
                char *end = strchr(p + 12, '<');
                if (end && end - (p + 12) < 30 && end - (p + 12) > 0) {
                    strncpy(ptoken, p + 12, end - (p + 12));
                    ptoken[end - (p + 12)] = '\0';
                    int v = atoi(ptoken);
                    if (v >= 1 && v <= 16) pnum = v;
                }
            }
            snprintf(ptoken, sizeof(ptoken), "%d", pnum);
            int cx = 1975, cy = 718;
            ptz_get_pos(&cx, &cy);
            char ppath[64];
            snprintf(ppath, sizeof(ppath), "/mnt/mtd/Config/preset_%d.txt", pnum);
            FILE *pf = fopen(ppath, "w");
            if (pf) { fprintf(pf, "%d %d\n", cx, cy); fclose(pf); }
            snprintf(resp_body, sizeof(resp_body),
                "%s<tptz:SetPresetResponse><tptz:PresetToken>%s</tptz:PresetToken></tptz:SetPresetResponse>%s",
                hdr, ptoken, ftr);
        } else if (strstr(buf, "GetServiceCapabilities")) {
            if (strstr(buf, "ver20/ptz") || strstr(buf, "ptz_service") || strcasestr(buf, "ptz")) {
                snprintf(resp_body, sizeof(resp_body),
                    "%s<tptz:GetServiceCapabilitiesResponse>"
                    "<tptz:Capabilities EFlip=\"false\" Reverse=\"false\" MoveStatus=\"true\" StatusPosition=\"true\"/>"
                    "</tptz:GetServiceCapabilitiesResponse>%s",
                    hdr, ftr);
            } else if (strstr(buf, "ver10/media") || strstr(buf, "media_service") || strcasestr(buf, "media")) {
                snprintf(resp_body, sizeof(resp_body),
                    "%s<trt:GetServiceCapabilitiesResponse>"
                    "<trt:Capabilities SnapshotUri=\"true\" Rotation=\"false\" VideoSourceMode=\"false\" OSD=\"false\"/>"
                    "</trt:GetServiceCapabilitiesResponse>%s",
                    hdr, ftr);
            } else {
                snprintf(resp_body, sizeof(resp_body),
                    "%s<tds:GetServiceCapabilitiesResponse>"
                    "<tds:Capabilities Network=\"false\" System=\"false\" Security=\"false\"/>"
                    "</tds:GetServiceCapabilitiesResponse>%s",
                    hdr, ftr);
            }
        } else if (strstr(buf, "GetNetworkInterfaces")) {
            char mac_str[32];
            get_local_mac(mac_str, sizeof(mac_str));
            snprintf(resp_body, sizeof(resp_body),
                "%s<tds:GetNetworkInterfacesResponse>"
                "<tds:NetworkInterfaces token=\"eth2\">"
                "<tt:Enabled>true</tt:Enabled>"
                "<tt:Info><tt:Name>eth2</tt:Name><tt:HwAddress>%s</tt:HwAddress><tt:MTU>1500</tt:MTU></tt:Info>"
                "<tt:IPv4><tt:Enabled>true</tt:Enabled><tt:Config><tt:Manual><tt:Address>%s</tt:Address><tt:PrefixLength>24</tt:PrefixLength></tt:Manual><tt:DHCP>true</tt:DHCP></tt:Config></tt:IPv4>"
                "</tds:NetworkInterfaces>"
                "</tds:GetNetworkInterfacesResponse>%s",
                hdr, mac_str, host_ip, ftr);
        } else if (strstr(buf, "GetScopes")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<tds:GetScopesResponse>"
                "<tds:Scopes><tt:ScopeDef>Fixed</tt:ScopeDef><tt:ScopeItem>onvif://www.onvif.org/type/video_encoder</tt:ScopeItem></tds:Scopes>"
                "<tds:Scopes><tt:ScopeDef>Fixed</tt:ScopeDef><tt:ScopeItem>onvif://www.onvif.org/type/ptz</tt:ScopeItem></tds:Scopes>"
                "<tds:Scopes><tt:ScopeDef>Fixed</tt:ScopeDef><tt:ScopeItem>onvif://www.onvif.org/name/CleanCam</tt:ScopeItem></tds:Scopes>"
                "<tds:Scopes><tt:ScopeDef>Fixed</tt:ScopeDef><tt:ScopeItem>onvif://www.onvif.org/hardware/GK7201V300</tt:ScopeItem></tds:Scopes>"
                "</tds:GetScopesResponse>%s",
                hdr, ftr);
        } else if (strstr(buf, "GetHostname")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<tds:GetHostnameResponse><tds:HostnameInformation><tt:FromDHCP>false</tt:FromDHCP><tt:Name>CleanCam</tt:Name></tds:HostnameInformation></tds:GetHostnameResponse>%s",
                hdr, ftr);
        } else if (strstr(buf, "GetUsers")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<tds:GetUsersResponse><tds:User><tt:Username>admin</tt:Username><tt:UserLevel>Administrator</tt:UserLevel></tds:User></tds:GetUsersResponse>%s",
                hdr, ftr);
        } else if (strstr(buf, "GetWsdlUrl")) {
            snprintf(resp_body, sizeof(resp_body),
                "%s<tds:GetWsdlUrlResponse><tds:WsdlUrl>http://www.onvif.org/ver10/device/wsdl</tds:WsdlUrl></tds:GetWsdlUrlResponse>%s",
                hdr, ftr);
        } else {
            snprintf(resp_body, sizeof(resp_body),
                "%s%s",
                hdr, ftr);
        }

        char resp_hdr[512];
        int hlen = snprintf(resp_hdr, sizeof(resp_hdr),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: %s\r\n"
                 "Content-Length: %zu\r\n"
                 "Connection: close\r\n\r\n",
                 content_type,
                 strlen(resp_body));
        send(cfd, resp_hdr, hlen, MSG_NOSIGNAL);
        send(cfd, resp_body, strlen(resp_body), MSG_NOSIGNAL);
        close(cfd);
    }
    return NULL;
}

static void *ws_discovery_thread(void *arg) {
    (void)arg;
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(WSD_PORT);
    sin.sin_addr.s_addr = INADDR_ANY;

    if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        printf("[WSD] Bind failed: %s\n", strerror(errno));
        close(fd);
        return NULL;
    }

    struct ip_mreq mreq;
    mreq.imr_multiaddr.s_addr = inet_addr("239.255.255.250");
    mreq.imr_interface.s_addr = INADDR_ANY;
    setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));

    printf("[WSD] WS-Discovery listening on UDP 3702...\n");

    char buf[2048];
    while (1) {
        struct sockaddr_in from;
        socklen_t flen = sizeof(from);
        ssize_t n = recvfrom(fd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from, &flen);
        if (n <= 0) continue;
        buf[n] = '\0';

        if (strstr(buf, "Probe") && (strstr(buf, "NetworkVideoTransmitter") || strstr(buf, "Device"))) {
            char msg_id[128] = "urn:uuid:00000000-0000-0000-0000-000000000000";
            char *mid = strstr(buf, "<wsa:MessageID>");
            if (mid) {
                char *mid_end = strstr(mid, "</wsa:MessageID>");
                if (mid_end && (mid_end - (mid + 15)) < (int)sizeof(msg_id)) {
                    int len = mid_end - (mid + 15);
                    strncpy(msg_id, mid + 15, len);
                    msg_id[len] = '\0';
                }
            }

            char cur_ip[32];
            strncpy(cur_ip, my_ip, sizeof(cur_ip) - 1);
            cur_ip[sizeof(cur_ip) - 1] = '\0';
            get_local_ip(cur_ip, sizeof(cur_ip));

            char reply[2048];
            int rlen = snprintf(reply, sizeof(reply),
                "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
                "<SOAP-ENV:Envelope xmlns:SOAP-ENV=\"http://www.w3.org/2003/05/soap-envelope\" xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" xmlns:d=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\" xmlns:dn=\"http://www.onvif.org/ver10/network/wsdl\">"
                "<SOAP-ENV:Header>"
                "<wsa:MessageID>urn:uuid:11223344-5566-7788-99aa-bbccddeeff00</wsa:MessageID>"
                "<wsa:RelatesTo>%s</wsa:RelatesTo>"
                "<wsa:To>http://schemas.xmlsoap.org/ws/2004/08/addressing/role/anonymous</wsa:To>"
                "<wsa:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/ProbeMatches</wsa:Action>"
                "</SOAP-ENV:Header>"
                "<SOAP-ENV:Body>"
                "<d:ProbeMatches>"
                "<d:ProbeMatch>"
                "<wsa:EndpointReference><wsa:Address>urn:uuid:clean-cam-gk7201v300</wsa:Address></wsa:EndpointReference>"
                "<d:Types>dn:NetworkVideoTransmitter</d:Types>"
                "<d:Scopes>onvif://www.onvif.org/type/video_encoder onvif://www.onvif.org/Profile/G onvif://www.onvif.org/Profile/S onvif://www.onvif.org/hardware/GK7201V300 onvif://www.onvif.org/name/CleanCam</d:Scopes>"
                "<d:XAddrs>http://%s:8899/onvif/device_service</d:XAddrs>"
                "<d:MetadataVersion>1</d:MetadataVersion>"
                "</d:ProbeMatch>"
                "</d:ProbeMatches>"
                "</SOAP-ENV:Body>"
                "</SOAP-ENV:Envelope>",
                msg_id, cur_ip);

            sendto(fd, reply, rlen, 0, (struct sockaddr *)&from, flen);
        }
    }
    return NULL;
}

static void crash_handler(int sig) {
    fprintf(stderr, "\n[CRASH] Caught fatal signal %d (%s)!\n", sig, strsignal(sig));
    fflush(stderr);
    _exit(128 + sig);
}

int main(int argc, char **argv) {
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
    signal(SIGPIPE, SIG_IGN);
    signal(SIGSEGV, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGILL, crash_handler);
    signal(SIGFPE, crash_handler);

    printf("=========================================\n");
    printf(" xm_rtspd - Native Dual-Stream RTSP/ONVIF\n");
    printf(" CleanCam GK7201V300 (Main 1080P & Sub D1)\n");
    printf("=========================================\n");

    // Initialize all client slots and their mutexes ONCE
    for (int i = 0; i < MAX_CLIENTS; i++) {
        memset(&clients[i], 0, sizeof(client_t));
        clients[i].fd = -1;
        pthread_mutex_init(&clients[i].send_lock, NULL);
    }

    // Initialize Stream 0 (Main)
    streams[0].stream_id = 0;
    streams[0].name = "Main";
    streams[0].codec = CODEC_H264;
    streams[0].rtp_seq = 1000;
    streams[0].rtp_timestamp = 10000;
    streams[0].rtp_ssrc = 0x12345678;
    pthread_mutex_init(&streams[0].lock, NULL);
    memcpy(streams[0].cached_sps, default_main_h264_sps, sizeof(default_main_h264_sps));
    streams[0].cached_sps_len = sizeof(default_main_h264_sps);
    memcpy(streams[0].cached_pps, default_main_h264_pps, sizeof(default_main_h264_pps));
    streams[0].cached_pps_len = sizeof(default_main_h264_pps);
    memcpy(streams[0].cached_vps, default_main_vps, sizeof(default_main_vps));
    streams[0].cached_vps_len = sizeof(default_main_vps);

    // Initialize Stream 1 (Sub)
    streams[1].stream_id = 1;
    streams[1].name = "Extra1";
    streams[1].codec = CODEC_H264;
    streams[1].rtp_seq = 5000;
    streams[1].rtp_timestamp = 20000;
    streams[1].rtp_ssrc = 0x87654321;
    pthread_mutex_init(&streams[1].lock, NULL);
    memcpy(streams[1].cached_sps, default_sub_h264_sps, sizeof(default_sub_h264_sps));
    streams[1].cached_sps_len = sizeof(default_sub_h264_sps);
    memcpy(streams[1].cached_pps, default_sub_h264_pps, sizeof(default_sub_h264_pps));
    streams[1].cached_pps_len = sizeof(default_sub_h264_pps);
    memcpy(streams[1].cached_vps, default_sub_vps, sizeof(default_sub_vps));
    streams[1].cached_vps_len = sizeof(default_sub_vps);

    // Auto-detect local IP address dynamically
    get_local_ip(my_ip, sizeof(my_ip));
    if (argc > 1 && argv[1][0] != '\0') {
        strncpy(my_ip, argv[1], sizeof(my_ip) - 1);
        my_ip[sizeof(my_ip) - 1] = '\0';
    }
    printf("[Init] Local IP: %s\n", my_ip);

    // Launch worker threads for both streams
    pthread_t th_main, th_sub;
    pthread_create(&th_main, NULL, dvrip_stream_thread, (void *)(intptr_t)0);
    pthread_create(&th_sub, NULL, dvrip_stream_thread, (void *)(intptr_t)1);

    // Launch ONVIF & WS-Discovery
    pthread_t th_onvif, th_wsd;
    pthread_create(&th_onvif, NULL, onvif_thread, NULL);
    pthread_create(&th_wsd, NULL, ws_discovery_thread, NULL);

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(RTSP_PORT);
    sin.sin_addr.s_addr = INADDR_ANY;

    if (bind(sfd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        perror("RTSP bind failed");
        return 1;
    }
    listen(sfd, 10);
    printf("[RTSP] Listening on port %d (Endpoints: /live/ch0, /live/ch1, /stream0, /stream1)...\n", RTSP_PORT);

    while (1) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        int cfd = accept(sfd, (struct sockaddr *)&caddr, &clen);
        if (cfd < 0) continue;

        struct timeval tv_snd = { .tv_sec = 2, .tv_usec = 0 };
        setsockopt(cfd, SOL_SOCKET, SO_SNDTIMEO, &tv_snd, sizeof(tv_snd));
        struct timeval tv_rcv = { .tv_sec = 3, .tv_usec = 0 };
        setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &tv_rcv, sizeof(tv_rcv));
        int nodelay = 1;
        setsockopt(cfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

        int slot = -1;
        pthread_mutex_lock(&clients_lock);
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i].in_use) {
                slot = i;
                clients[i].in_use = 1;
                clients[i].active = 1;
                clients[i].is_playing = 0;
                clients[i].broken = 0;
                clients[i].fd = cfd;
                clients[i].stream_id = 0;
                clients[i].session_id = 0x55AA1234 + i;
                clients[i].addr = caddr;
                clients[i].client_rtp_port = 0;
                clients[i].client_rtcp_port = 0;
                clients[i].udp_rtp_fd = 0;
                clients[i].udp_audio_rtp_fd = 0;
                clients[i].video_setup = 0;
                clients[i].audio_setup = 0;
                clients[i].video_channel = 0;
                clients[i].audio_channel = 2;
                clients[i].client_audio_rtp_port = 0;
                clients[i].client_audio_rtcp_port = 0;
                clients[i].rtp_interleaved = 0;
                clients[i].authenticated = 0;
                clients[i].video_rtp_seq = 1000 + i * 10;
                clients[i].audio_rtp_seq = 100 + i * 10;
                inet_ntop(AF_INET, &caddr.sin_addr, clients[i].client_ip, sizeof(clients[i].client_ip));
                break;
            }
        }
        pthread_mutex_unlock(&clients_lock);

        if (slot < 0) {
            printf("[RTSP] Max clients (%d) reached, rejecting.\n", MAX_CLIENTS);
            close(cfd);
            continue;
        }

        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setstacksize(&attr, 64 * 1024);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

        pthread_t tid;
        if (pthread_create(&tid, &attr, rtsp_client_thread, (void *)(intptr_t)slot) != 0) {
            printf("[RTSP] Failed to spawn thread for client slot %d\n", slot);
            pthread_mutex_lock(&clients[slot].send_lock);
            clients[slot].active = 0;
            clients[slot].is_playing = 0;
            clients[slot].broken = 1;
            clients[slot].fd = -1;
            pthread_mutex_unlock(&clients[slot].send_lock);

            pthread_mutex_lock(&clients_lock);
            clients[slot].in_use = 0;
            pthread_mutex_unlock(&clients_lock);

            close(cfd);
        }
        pthread_attr_destroy(&attr);
    }

    return 0;
}

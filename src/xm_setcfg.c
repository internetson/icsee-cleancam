#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

static int recv_all(int fd, uint8_t *buf, size_t count) {
    size_t total = 0;
    while (total < count) {
        ssize_t n = recv(fd, buf + total, count - total, 0);
        if (n <= 0) return -1;
        total += n;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: xm_setcfg <msg_id> <json_payload>\n");
        return 1;
    }

    uint16_t msg_id = (uint16_t)atoi(argv[1]);
    const char *payload_json = argv[2];

    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return 1;

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(34567);
    sin.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        close(s);
        return 1;
    }

    // 1. Login
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

    if (recv_all(s, hdr, 20) < 0) { close(s); return 1; }
    uint32_t resp_len = *(uint32_t *)(hdr + 16);
    char *resp_buf = malloc(resp_len + 1);
    if (recv_all(s, (uint8_t *)resp_buf, resp_len) < 0) { free(resp_buf); close(s); return 1; }
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

    // 2. Prepare payload with SessionID injected
    char sess_hex[32];
    snprintf(sess_hex, sizeof(sess_hex), "0x%08X", session_id);

    char full_payload[4096];
    // Check if SessionID placeholder exists or inject before trailing '}'
    if (strstr(payload_json, "SESSION_ID_PLACEHOLDER")) {
        char *pos = strstr(payload_json, "SESSION_ID_PLACEHOLDER");
        size_t pre_len = pos - payload_json;
        snprintf(full_payload, sizeof(full_payload), "%.*s%s%s\n", (int)pre_len, payload_json, sess_hex, pos + strlen("SESSION_ID_PLACEHOLDER"));
    } else {
        // Inject SessionID
        size_t plen = strlen(payload_json);
        if (plen > 0 && payload_json[plen - 1] == '}') {
            snprintf(full_payload, sizeof(full_payload), "%.*s,\"SessionID\":\"%s\"}\n", (int)(plen - 1), payload_json, sess_hex);
        } else {
            snprintf(full_payload, sizeof(full_payload), "%s\n", payload_json);
        }
    }

    uint32_t plen = strlen(full_payload) + 1;
    uint32_t two = 2;
    memcpy(hdr + 4, &session_id, 4);
    memcpy(hdr + 8, &two, 4);
    memcpy(hdr + 14, &msg_id, 2);
    memcpy(hdr + 16, &plen, 4);

    send(s, hdr, 20, 0);
    send(s, full_payload, plen, 0);

    if (recv_all(s, hdr, 20) == 0) {
        uint32_t rlen = *(uint32_t *)(hdr + 16);
        char *rbuf = malloc(rlen + 1);
        recv_all(s, (uint8_t *)rbuf, rlen);
        rbuf[rlen] = '\0';
        printf("%s\n", rbuf);
        free(rbuf);
    }

    close(s);
    return 0;
}

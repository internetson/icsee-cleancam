#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in sa = {0};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(80);
    int r = bind(fd, (struct sockaddr*)&sa, sizeof(sa));
    printf("bind 80 result: %d, errno: %d\n", r, errno);
    return 0;
}

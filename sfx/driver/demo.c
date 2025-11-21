#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>

#define DEVICE "/dev/mctp_bridge"
#define MAX_BUF 2048

/* 将字符串中的 hex token 转成 bytes */
int parse_hex_string(const char *input, uint8_t *output, int max_len)
{
    int count = 0;
    const char *p = input;

    while (*p && count < max_len) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;

        if (!*p)
            break;

        unsigned int byte;
        if (sscanf(p, "%02x", &byte) != 1)
            return -1;

        output[count++] = (uint8_t)byte;

        /* 跳到下一个 token */
        while (*p && *p != ' ')
            p++;
    }
    return count;
}

/* 打印 hex buffer */
void print_hex(const uint8_t *buf, int len)
{
    for (int i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
        if (i % 16 == 15)
            printf("\n");
    }
    if (len % 16 != 0)
        printf("\n");
}

int main(void)
{
    int fd = open(DEVICE, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    printf("Opened %s successfully.\n", DEVICE);
    printf("Waiting for incoming MCTP frames via poll() ...\n");
    printf("You can also type hex bytes to send (ex: 01 02 FF 33):\n");

    struct pollfd pfd = {
        .fd = fd,
        .events = POLLIN,
    };

    while (1) {
        int ret = poll(&pfd, 1, 500);  // 500 ms timeout
        if (ret < 0) {
            perror("poll");
            break;
        }

        /* 有数据可读 */
        if (ret > 0 && (pfd.revents & POLLIN)) {
            uint8_t buf[MAX_BUF];
            int n = read(fd, buf, sizeof(buf));
            if (n < 0) {
                perror("read");
                break;
            }

            printf("\n[RECV %d bytes]:\n", n);
            printf("%s\n", buf+5);
            print_hex(buf, n);
        }

        /* 非阻塞检测用户输入 */
        fd_set rfds;
        struct timeval tv = {0, 0};
        FD_ZERO(&rfds);
        FD_SET(0, &rfds);

        int has_input = select(1, &rfds, NULL, NULL, &tv);
        if (has_input > 0 && FD_ISSET(0, &rfds)) {
            char line[4096];

            if (!fgets(line, sizeof(line), stdin))
                continue;

            uint8_t out_buf[MAX_BUF];
            int len = parse_hex_string(line, out_buf, sizeof(out_buf));
            if (len < 0) {
                printf("Invalid hex format. Example: 01 02 FF A0\n");
                continue;
            }

            printf("[SEND %d bytes]\n", len);
            print_hex(out_buf, len);

            int w = write(fd, out_buf, len);
            if (w < 0)
                perror("write");
        }
    }

    close(fd);
    return 0;
}

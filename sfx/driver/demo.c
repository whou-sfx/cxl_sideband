#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <linux/mctp.h>

#define DEVICE "/dev/mctp_bridge"
#define MAX_BUF 2048

#define MSG_TYPE_PLDM 1
#define DEV_EID 8
#define HOST_EID 1

/* MCTP packet definitions */
struct mctp_hdr {
    uint8_t  ver;
    uint8_t  dst;                                                                                                                 
    uint8_t  src;
    uint8_t  flags_seq_tag;
};

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

int bridge_send_to_dev(uint8_t *buf, int len)
{
    struct mctp_hdr *hdr = (struct mctp_hdr *)buf;
    uint8_t msg_type = *(buf + sizeof(struct mctp_hdr));
    char *payload = buf + sizeof(struct mctp_hdr) + 1;
    printf("hdr[ver: %x, dst: %x, src:%x, flags_tag: %x], msg_type: %x, len: %x]\n",
           hdr->ver, hdr->dst, hdr->src, hdr->flags_seq_tag, msg_type, len);

    printf("%s\n", payload);
    return 0;
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
            print_hex(buf, n);
            bridge_send_to_dev(buf, n);
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
            struct mctp_hdr *hdr = (struct mctp_hdr *)out_buf;
            memset(out_buf, 0, sizeof(out_buf));
            hdr->ver = 0x01;
            hdr->dst = HOST_EID;
            hdr->src = DEV_EID;
            hdr->flags_seq_tag = 0xC8;
            out_buf[4]  = MSG_TYPE_PLDM;

            int len = parse_hex_string(line, out_buf + 5, sizeof(out_buf) - 5);
            if (len < 0) {
                printf("Invalid hex format. Example: 01 02 FF A0\n");
                continue;
            }
            len += 5;

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

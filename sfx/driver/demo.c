#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <linux/mctp.h>
#include "pldm.h"
#include "mctp.h"
#include "cxl_cci.h"

#define DEVICE "/dev/mctp_bridge"
#define MAX_BUF 2048

#define DEV_EID 8
#define HOST_EID 19

uint8_t cur_tag = 0;
int g_fd = 0;


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

int handle_cci_req(unsigned char *buf, int len)
{
    struct mctp_hdr *hdr = (struct mctp_hdr *)buf;
    uint8_t tmp_eid = hdr->dst;
    hdr->dst = hdr->src;
    hdr->src = tmp_eid;
    hdr->flags_seq_tag &= 0xF7; /*clear the TO bits*/

    struct cxlmi_cci_msg *cci_msg = (struct cxlmi_cci_msg *)(buf + sizeof(struct mctp_hdr) + 1);

    printf("cci command [0x%x 0x%x]\n", cci_msg->command_set, cci_msg->command);

    cci_msg->return_code = 0;
    cci_msg->category = 1;
    
    return write_to_host(buf, len);
}

int handle_req_from_host(uint8_t *buf, int len)
{
    struct mctp_hdr *hdr = (struct mctp_hdr *)buf;
    uint8_t msg_type = *(buf + sizeof(struct mctp_hdr));
    char *payload = buf + sizeof(struct mctp_hdr) + 1;
    printf("mctp hdr[ver: %x, dst: %x, src:%x, flags_tag: %x], msg_type: %x, len: %x]\n",
           hdr->ver, hdr->dst, hdr->src, hdr->flags_seq_tag, msg_type, len);
    cur_tag = hdr->flags_seq_tag;

    // 判断是否是PLDM消息
    if (msg_type == MSG_TYPE_PLDM) {
        printf("PLDM message received, handling PLDM request\n");
        return handle_pldm_req(buf, len);
    } else if (msg_type == MSG_TYPE_CXLCCI) {
        printf("CXLCCI message received, handling CCI request\n");
        return handle_cci_req(buf, len);

    } else {
        printf("Non-PLDM message type: 0x%02x\n", msg_type);
        printf("Payload: %s\n", payload);
    }
    return 0;
}

int write_to_host(uint8_t * out_buf, int len)
{
    if (g_fd <= 0) {
        printf("device not opened\n");
        return -1;
    }
  
    printf("[SEND TO HOST %d bytes]\n", len);
    print_hex(out_buf, len);
    int w = write(g_fd, out_buf, len);
    if (w < 0) {
        perror("write");
    }
    return w;
 
}

int main(void)
{
    int fd = open(DEVICE, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }
    g_fd = fd;

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
            handle_req_from_host(buf, n);
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
            hdr->flags_seq_tag = cur_tag & 0xF7; /*mask the TO bit*/
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

    g_fd = -1;
    close(fd);
    return 0;
}

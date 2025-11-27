/*************************************************************************
@File Name: mctp_send.c
@Desc: 
@Author: Andy-wei.hou
@Mail: wei.hou@scaleflux.com 
@Created Time: 2025年11月20日 星期四 20时28分53秒
@Log: 
************************************************************************/
// SPDX-License-Identifier: MIT
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <linux/mctp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h> // Added for poll

#define LOCAL_EID 19
#define DEST_EID  8
#define PLDM_TYPE 1

int main(void) {
    int sock;
    struct sockaddr_mctp src, dst;
    uint8_t buf[64];
    ssize_t sent;
    size_t pldm_len = 20; // 示例 PLDM payload

    // 1) 创建 AF_MCTP datagram socket
    sock = socket(AF_MCTP, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    /** // 2) bind 本地 EID */
    memset(&src, 0, sizeof(src));
    src.smctp_family = AF_MCTP;
    src.smctp_network = MCTP_NET_ANY;
    src.smctp_addr.s_addr = MCTP_ADDR_ANY;
    src.smctp_type = PLDM_TYPE;
    src.smctp_tag = MCTP_TAG_OWNER;

    if (bind(sock, (struct sockaddr*)&src, sizeof(src)) < 0) {
        perror("bind");
        close(sock);
        return -1;
    }


    // 3) 构造 payload
    /* arbitrary message to send, with message-type header */
    buf[0] = PLDM_TYPE;
    snprintf(buf + 1, sizeof(buf) -1, (char *)"hello, world!");
    printf("%s\n", buf + 1);


    // 4) 设置目标地址
    memset(&dst, 0, sizeof(dst));
    dst.smctp_family = AF_MCTP;
    dst.smctp_network = 1;
    dst.smctp_addr.s_addr = DEST_EID;
    dst.smctp_type = PLDM_TYPE;
    dst.smctp_tag = MCTP_TAG_OWNER;

    // 5) 发送
    sent = sendto(sock, buf, 1 + pldm_len, 0,
                  (struct sockaddr*)&dst, sizeof(dst));
    if (sent < 0) {
        perror("sendto");
        close(sock);
        return -1;
    }

    printf("Sent %zd bytes over AF_MCTP\n", sent);

    while (1) {
        // 6) 设置polling等待接收数据
        struct pollfd fds[1];
        fds[0].fd = sock;
        fds[0].events = POLLIN;

        int ret = poll(fds, 1, 5000); // 等待5秒
        if (ret < 0) {
            perror("poll");
            close(sock);
            return -1;
        } else if (ret == 0) {
            printf("Timeout: no data received within 5 seconds\n");
        } else {
            // 7) 接收数据
            uint8_t recv_buf[128];
            struct sockaddr_mctp from_addr;
            socklen_t from_len = sizeof(from_addr);

            ssize_t received = recvfrom(sock, recv_buf, sizeof(recv_buf), 0,
                                    (struct sockaddr*)&from_addr, &from_len);
            if (received < 0) {
                perror("recvfrom");
            } else {
                printf("Received %zd bytes from EID %u:, msgType: %x\n", received, from_addr.smctp_addr.s_addr
                    , from_addr.smctp_type);
                printf("Data: ");
                for (int i = 0; i < received; i++) {
                    printf("%02x ", recv_buf[i]);
                }
                printf("\n");

                // 如果数据是文本，也打印文本内容
                if (received > 0 && recv_buf[0] == PLDM_TYPE) {
                    printf("Text: %s\n", recv_buf + 1);
                }
            }
        }
    }

    close(sock);
    return 0;
}


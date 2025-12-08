#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <linux/mctp.h>
#include <pthread.h>
#include "i2c_proxy_aardvark.h"


#define HOST_DEV "/dev/mctp_bridge"
int g_host_fd  = -1;

/* 打印 hex buffer */
void print_hex(const u8 *buf, int len)
{
    for (int i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
        if (i % 16 == 15)
            printf("\n");
    }
    if (len % 16 != 0)
        printf("\n");
}

int write_to_host(u8 * mctp_buf, int len)
{
    if (g_host_fd <= 0) {
        printf("device not opened\n");
        return -1;
    }
  
    printf("[SEND TO HOST %d bytes]\n", len);
    print_hex(mctp_buf, len);
    int w = write(g_host_fd, mctp_buf, len);
    if (w < 0) {
        perror("write");
    }
    return w;
 
}

static int handle_req_from_host(u8 *buf, int len)
{
    return i2c_handle_req_from_host(buf, len);
}


/* host_rx_thread: 从 /dev/mctp_bridge 读取 AF_MCTP 报文，构造 SMBus 流，放队列 */
static void *host_rx_thread(void *arg) {
    int fd = *(int*)arg; /* 已打开的 host device fd */
    u8 buf[4096];
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

            memset(buf, 0x00, sizeof(buf));
            int n = read(fd, buf, sizeof(buf));
            if (n < 0) {
                perror("read");
                break;
            }
            printf("\n[RECV %d bytes]:\n", n);
            print_hex(buf, n);
            handle_req_from_host(buf, n);
        }

    }
    return NULL;
}

int main(int argc, char **argv) {
    /* 打开 host device */
    g_host_fd = open(HOST_DEV, O_RDONLY | O_RDWR);
    if (g_host_fd < 0) { 
        perror("open host dev"); 
        return 1; 
     }
    if (i2c_proxy_init()) {
        perror("init i2c proxy fail");
        return 1;
    }

    pthread_t th_host;
    pthread_create(&th_host, NULL, host_rx_thread, &g_host_fd);

    /* 主线程可做其他事或等待 */
    pthread_join(th_host, NULL);
    i2c_proxy_close();
    /* 清理 */
    close(g_host_fd);
    return 0;
}


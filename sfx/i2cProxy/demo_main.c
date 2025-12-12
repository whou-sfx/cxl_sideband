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

static void print_usage(const char *progname)
{
    printf("Usage: %s [options]\n", progname);
    printf("Options:\n");
    printf("  --haddr ADDR       Host address (g_lsrc) in hex (default: 0x41)\n");
    printf("  --daddr ADDR       Device address (g_ldevdst) in hex (default: 0x44)\n");
    printf("  --freq KHZ         I2C bitrate in kHz (default: 100)\n");
    printf("  --timeout MS       Slave poll timeout in ms (default: 5000)\n");
    printf("  --help             Print this help message\n");
}

int main(int argc, char **argv) {
    struct i2c_proxy_params params = {
        .haddr = 0x41,
        .daddr = 0x44,
        .freq_khz = 100,
        .timeout_ms = 5000,
    };

    /* Parse command line arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--haddr") == 0) {
            if (i + 1 < argc) {
                params.haddr = (u8)strtol(argv[i + 1], NULL, 0);
                i++;
            } else {
                fprintf(stderr, "Error: --haddr requires an argument\n");
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--daddr") == 0) {
            if (i + 1 < argc) {
                params.daddr = (u8)strtol(argv[i + 1], NULL, 0);
                i++;
            } else {
                fprintf(stderr, "Error: --daddr requires an argument\n");
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--freq") == 0) {
            if (i + 1 < argc) {
                params.freq_khz = atoi(argv[i + 1]);
                if (params.freq_khz <= 0) {
                    fprintf(stderr, "Error: --freq must be positive\n");
                    return 1;
                }
                i++;
            } else {
                fprintf(stderr, "Error: --freq requires an argument\n");
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--timeout") == 0) {
            if (i + 1 < argc) {
                params.timeout_ms = atoi(argv[i + 1]);
                if (params.timeout_ms <= 0) {
                    fprintf(stderr, "Error: --timeout must be positive\n");
                    return 1;
                }
                i++;
            } else {
                fprintf(stderr, "Error: --timeout requires an argument\n");
                print_usage(argv[0]);
                return 1;
            }
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Error: Unknown option '%s'\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    /* 打开 host device */
    g_host_fd = open(HOST_DEV, O_RDONLY | O_RDWR);
    if (g_host_fd < 0) {
        perror("open host dev");
        return 1;
     }
    printf("open %s success\n", HOST_DEV);

    if (i2c_proxy_init(&params)) {
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


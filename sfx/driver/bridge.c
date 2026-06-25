#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <linux/mctp.h>
#include "pldm.h"
#include "mctp.h"
#include "cxl_cci.h"
#include "util.h"

#define DEVICE "/dev/mctp_bridge"
#define MAX_BUF 2048

#define DEV_EID 8
#define HOST_EID 19
#define MSG_TYPE_CONTROL 0

uint8_t cur_tag = 0;
int g_fd = 0;

static volatile sig_atomic_t g_running = 1;
static int g_udp_fd = -1;

static void cleanup_all(void)
{
    if (g_udp_fd >= 0) {
        close(g_udp_fd);
        g_udp_fd = -1;
    }
    if (g_fd > 0) {
        close(g_fd);
        g_fd = -1;
    }
}

static void on_signal(int sig)
{
    g_running = 0;
}

static void setup_signals(void)
{
    struct sigaction sa = {0};
    sa.sa_handler = on_signal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
}

struct udp_config {
    bool enabled;
    int sockfd;
    struct sockaddr_storage target_addr;
    socklen_t target_addrlen;
    uint16_t listen_port;
};

struct control_request {
    bool enabled;
    uint8_t opcode;
    uint8_t data[MAX_BUF];
    int data_len;
};

static void print_usage(const char *prog)
{
    printf("Usage: %s [--udp host:port --listen-port port [--no-bridge] [--once] [--control ctrl-opc] [--control-data \"hex bytes\"]]\n", prog);
}

static int parse_port(const char *s, uint16_t *port)
{
    char *end = NULL;
    long value = strtol(s, &end, 10);

    if (!s[0] || (end && *end) || value <= 0 || value > 65535)
        return -1;

    *port = (uint16_t)value;
    return 0;
}

static int parse_udp_target(const char *arg, char *host, size_t host_len, uint16_t *port)
{
    const char *sep = strrchr(arg, ':');
    size_t len;

    if (!sep || sep == arg)
        return -1;

    len = (size_t)(sep - arg);
    if (len == 0 || len >= host_len)
        return -1;

    memcpy(host, arg, len);
    host[len] = '\0';

    return parse_port(sep + 1, port);
}

static int udp_init(struct udp_config *cfg, const char *target, uint16_t listen_port)
{
    struct addrinfo hints = {0};
    struct addrinfo *result = NULL;
    struct addrinfo *rp;
    char host[256];
    uint16_t target_port;
    char port_str[16];
    int sockfd = -1;
    int ret;

    if (parse_udp_target(target, host, sizeof(host), &target_port) < 0) {
        fprintf(stderr, "Invalid UDP target '%s', expected host:port\n", target);
        return -1;
    }

    snprintf(port_str, sizeof(port_str), "%u", target_port);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;

    ret = getaddrinfo(host, port_str, &hints, &result);
    if (ret != 0) {
        fprintf(stderr, "getaddrinfo(%s): %s\n", host, gai_strerror(ret));
        return -1;
    }

    for (rp = result; rp; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd < 0)
            continue;

        if (listen_port != 0) {
            if (rp->ai_family == AF_INET) {
                struct sockaddr_in local = {0};
                local.sin_family = AF_INET;
                local.sin_addr.s_addr = htonl(INADDR_ANY);
                local.sin_port = htons(listen_port);
                if (bind(sockfd, (struct sockaddr *)&local, sizeof(local)) < 0) {
                    close(sockfd);
                    sockfd = -1;
                    continue;
                }
            } else if (rp->ai_family == AF_INET6) {
                struct sockaddr_in6 local6 = {0};
                local6.sin6_family = AF_INET6;
                local6.sin6_addr = in6addr_any;
                local6.sin6_port = htons(listen_port);
                if (bind(sockfd, (struct sockaddr *)&local6, sizeof(local6)) < 0) {
                    close(sockfd);
                    sockfd = -1;
                    continue;
                }
            }
        }

        if (connect(sockfd, rp->ai_addr, rp->ai_addrlen) == 0) {
            memcpy(&cfg->target_addr, rp->ai_addr, rp->ai_addrlen);
            cfg->target_addrlen = rp->ai_addrlen;
            cfg->sockfd = sockfd;
            cfg->listen_port = listen_port;
            cfg->enabled = true;
            break;
        }

        close(sockfd);
        sockfd = -1;
    }

    freeaddrinfo(result);

    if (sockfd < 0) {
        perror("udp setup");
        return -1;
    }

    printf("UDP forwarding enabled: target=%s listen_port=%u\n", target, listen_port);
    return 0;
}

static void print_mctp_meta(const char *prefix, const uint8_t *buf, int len)
{
    if (len < (int)sizeof(struct mctp_hdr) + 1) {
        printf("[%s meta] short frame len=%d\n", prefix, len);
        return;
    }

    const struct mctp_hdr *hdr = (const struct mctp_hdr *)buf;
    uint8_t msg_type = buf[sizeof(struct mctp_hdr)];
    uint8_t flags = hdr->flags_seq_tag;
    uint8_t tag = flags & 0x7;
    uint8_t seq = (flags >> 4) & 0x3;
    uint8_t to = (flags >> 3) & 0x1;

    printf("[%s meta] ver=0x%02x dst_eid=%u src_eid=%u flags=0x%02x tag=%u seq=%u to=%u msg_type=0x%02x payload_len=%d\n",
           prefix, hdr->ver, hdr->dst, hdr->src, flags, tag, seq, to,
           msg_type, len - (int)sizeof(struct mctp_hdr) - 1);

    if (msg_type == MSG_TYPE_PLDM && len >= (int)sizeof(struct mctp_hdr) + 1 + 3) {
        const uint8_t *pldm = buf + sizeof(struct mctp_hdr) + 1;
        uint8_t hdr0 = pldm[0];
        printf("[%s pldm] hdr=0x%02x rq=%u d=%u instance_id=%u type=0x%02x cmd=0x%02x\n",
               prefix, hdr0, (hdr0 >> 7) & 0x1, (hdr0 >> 6) & 0x1,
               hdr0 & 0x1f, pldm[1], pldm[2]);
    }
}

int handle_cci_req(unsigned char *buf, int len)
{
    struct mctp_hdr *hdr = (struct mctp_hdr *)buf;
    uint8_t tmp_eid = hdr->dst;
    hdr->dst = hdr->src;
    hdr->src = tmp_eid;
    hdr->flags_seq_tag &= 0xF7;

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
    char *payload = (char *)(buf + sizeof(struct mctp_hdr) + 1);
    printf("mctp hdr[ver: %x, dst: %x, src:%x, flags_tag: %x], msg_type: %x, len: %x]\n",
           hdr->ver, hdr->dst, hdr->src, hdr->flags_seq_tag, msg_type, len);
    cur_tag = hdr->flags_seq_tag;

    if (msg_type == MSG_TYPE_PLDM) {
        printf("PLDM message received, handling PLDM request\n");
        return handle_pldm_req(buf, len);
    } else if (msg_type == MSG_TYPE_CXLCCI) {
        printf("CXLCCI message received, handling CCI request\n");
        return handle_cci_req(buf, len);
    }

    printf("Non-PLDM message type: 0x%02x\n", msg_type);
    print_hex((const uint8_t *)payload, len - (int)sizeof(struct mctp_hdr) - 1);
    return 0;
}

int write_to_host(uint8_t *out_buf, int len)
{
    if (g_fd <= 0) {
        printf("device not opened\n");
        return -1;
    }

    printf("[SEND TO HOST %d bytes]\n", len);
    print_mctp_meta("SEND TO HOST", out_buf, len);
    print_hex(out_buf, len);
    int w = write(g_fd, out_buf, len);
    if (w < 0)
        perror("write");
    return w;
}

static int udp_forward_to_peer(struct udp_config *cfg, uint8_t *buf, int len)
{
    int sent = send(cfg->sockfd, buf, len, 0);

    if (sent < 0) {
        perror("udp send");
        return -1;
    }

    printf("[FORWARD TO UDP %d bytes]\n", sent);
    print_mctp_meta("FORWARD TO UDP", buf, sent);
    print_hex(buf, sent);
    return sent;
}

static int send_control_request(int fd, struct udp_config *udp, bool no_bridge,
                                const struct control_request *control)
{
    uint8_t out_buf[MAX_BUF] = {0};
    struct mctp_hdr *hdr = (struct mctp_hdr *)out_buf;
    int len;

    if (!control->enabled)
        return 0;

    hdr->ver = 0x01;
    hdr->dst = DEV_EID;
    hdr->src = HOST_EID;
    hdr->flags_seq_tag = 0xC8;
    out_buf[4] = MSG_TYPE_CONTROL;
    out_buf[5] = 0x80;
    out_buf[6] = control->opcode;
    if (control->data_len > 0)
        memcpy(out_buf + 7, control->data, control->data_len);
    len = 7 + control->data_len;

    printf("[SEND CONTROL %d bytes]\n", len);
    print_hex(out_buf, len);

    if (udp->enabled)
        return udp_forward_to_peer(udp, out_buf, len);
    if (no_bridge) {
        printf("[NO-BRIDGE] skip write_to_host\n");
        return len;
    }
    return write(fd, out_buf, len);
}

static int udp_forward_to_bridge(struct udp_config *cfg, bool no_bridge)
{
    uint8_t buf[MAX_BUF];
    int n = recv(cfg->sockfd, buf, sizeof(buf), 0);

    if (n < 0) {
        perror("udp recv");
        return -1;
    }

    if (n == 0)
        return 0;

    printf("[RECV FROM UDP %d bytes]\n", n);
    print_mctp_meta("RECV FROM UDP", buf, n);
    print_hex(buf, n);
    if (no_bridge) {
        printf("[MOCK BRIDGE RX %d bytes]\n", n);
        print_hex(buf, n);
        printf("[NO-BRIDGE] skip write_to_host\n");
        return n;
    }
    return write_to_host(buf, n);
}

int main(int argc, char **argv)
{
    struct udp_config udp = {0};
    struct control_request control = {0};
    const char *udp_target = NULL;
    const char *control_data = NULL;
    uint16_t listen_port = 0;
    bool no_bridge = false;
    bool once = false;
    int fd = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--udp") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            udp_target = argv[++i];
        } else if (strcmp(argv[i], "--listen-port") == 0) {
            if (i + 1 >= argc || parse_port(argv[i + 1], &listen_port) < 0) {
                print_usage(argv[0]);
                return 1;
            }
            i++;
        } else if (strcmp(argv[i], "--no-bridge") == 0) {
            no_bridge = true;
        } else if (strcmp(argv[i], "--once") == 0) {
            once = true;
        } else if (strcmp(argv[i], "--control") == 0) {
            unsigned int opcode;
            if (i + 1 >= argc || sscanf(argv[i + 1], "%x", &opcode) != 1 || opcode > 0xff) {
                print_usage(argv[0]);
                return 1;
            }
            control.enabled = true;
            control.opcode = (uint8_t)opcode;
            i++;
        } else if (strcmp(argv[i], "--control-data") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return 1;
            }
            control_data = argv[++i];
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    if ((udp_target != NULL) != (listen_port != 0)) {
        fprintf(stderr, "Both --udp and --listen-port are required for UDP mode\n");
        return 1;
    }

    if (control_data) {
        control.data_len = parse_hex_string(control_data, control.data, sizeof(control.data));
        if (control.data_len < 0) {
            fprintf(stderr, "Invalid control data format. Example: 08 00\n");
            return 1;
        }
    }

    if (!no_bridge) {
        fd = open(DEVICE, O_RDWR);
        if (fd < 0) {
            perror("open");
            return 1;
        }
        g_fd = fd;
        printf("Opened %s successfully.\n", DEVICE);
        printf("Waiting for incoming MCTP frames via poll() ...\n");
    } else {
        printf("Running in no-bridge mode.\n");
    }

    if (udp_target && udp_init(&udp, udp_target, listen_port) < 0) {
        if (fd >= 0)
            close(fd);
        return 1;
    }

    if (udp.enabled)
        g_udp_fd = udp.sockfd;
    atexit(cleanup_all);
    setup_signals();

    if (send_control_request(fd, &udp, no_bridge, &control) < 0)
        return 1;

    if (!udp.enabled || no_bridge)
        printf("You can also type hex bytes to send (ex: 01 02 FF 33):\n");

    while (g_running) {
        struct pollfd pfds[2];
        nfds_t nfds = 0;
        int ret;

        if (fd >= 0) {
            pfds[nfds].fd = fd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }

        if (udp.enabled) {
            pfds[nfds].fd = udp.sockfd;
            pfds[nfds].events = POLLIN;
            pfds[nfds].revents = 0;
            nfds++;
        }

        ret = poll(pfds, nfds, 500);
        if (ret < 0) {
            if (errno == EINTR || !g_running)
                break;
            perror("poll");
            break;
        }

        if (fd >= 0 && ret > 0 && (pfds[0].revents & POLLIN)) {
            uint8_t buf[MAX_BUF];
            int n = read(fd, buf, sizeof(buf));
            if (n < 0) {
                perror("read");
                break;
            }

            printf("\n[RECV %d bytes]:\n", n);
            print_mctp_meta("RECV FROM HOST", buf, n);
            print_hex(buf, n);
            if (udp.enabled)
                udp_forward_to_peer(&udp, buf, n);
            else
                handle_req_from_host(buf, n);
            if (once)
                break;
        }

        if (udp.enabled) {
            int udp_index = (fd >= 0) ? 1 : 0;
            if (ret > 0 && (pfds[udp_index].revents & POLLIN)) {
                if (udp_forward_to_bridge(&udp, no_bridge) < 0)
                    break;
                if (once)
                    break;
            }
        }

        if (!udp.enabled || no_bridge) {
            fd_set rfds;
            struct timeval tv = {0, 0};
            FD_ZERO(&rfds);
            FD_SET(0, &rfds);

            int has_input = select(1, &rfds, NULL, NULL, &tv);
            if (has_input > 0 && FD_ISSET(0, &rfds)) {
                char line[4096];
                uint8_t out_buf[MAX_BUF];
                struct mctp_hdr *hdr = (struct mctp_hdr *)out_buf;
                int len;

                if (!fgets(line, sizeof(line), stdin))
                    break;

                if (udp.enabled && no_bridge) {
                    len = parse_hex_string(line, out_buf, sizeof(out_buf));
                    if (len < 0) {
                        printf("Invalid hex format. Example: 01 02 FF A0\n");
                        continue;
                    }

                    printf("[SEND UDP %d bytes]\n", len);
                    print_hex(out_buf, len);
                    udp_forward_to_peer(&udp, out_buf, len);
                    if (once)
                        break;
                    continue;
                }

                memset(out_buf, 0, sizeof(out_buf));
                hdr->ver = 0x01;
                hdr->dst = HOST_EID;
                hdr->src = DEV_EID;
                hdr->flags_seq_tag = cur_tag & 0xF7;
                out_buf[4] = MSG_TYPE_PLDM;

                len = parse_hex_string(line, out_buf + 5, sizeof(out_buf) - 5);
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
    }

    if (udp.enabled) {
        close(udp.sockfd);
        g_udp_fd = -1;
    }
    g_fd = -1;
    if (fd >= 0)
        close(fd);
    return 0;
}

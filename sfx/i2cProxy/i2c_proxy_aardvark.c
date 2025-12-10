/* aardvark_integration.c - 示例整合片段 (精简示例) */
#include "i2c_proxy_aardvark.h"
#include "aardvark.h" /* 你提供的 aardvark.h */
#include "demo_main.h"
#include "mctp.h"
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


/* 配置 */
#define AARDVARK_PORT 0     /* 或者 detect/枚举 */
#define I2C_BITRATE_KHZ 100 /* 100kHz/400kHz 等 */
#define TLV_SEND_TIMEOUT_MS 500
#define AARDVARK_MTU 1024

#define I2C_DEVICE_SLAVE_ADDR 0x42
#define I2C_MASTER_SOURCE_ADDR 0x01
#define MCTP_I2C_COMMANDCODE 0x0F

/* Aardvark device handle and lock */
static Aardvark aardvark = -1;
static pthread_mutex_t aardvark_lock = PTHREAD_MUTEX_INITIALIZER;

/*0 for send ; 1 for recv*/
static u8 Aardvark_buf[2][AARDVARK_MTU];
#define MCTP_I2C_HDR_LEN   (sizeof(struct mctp_i2c_hdr))

u8 g_ldevdst = 0x42, g_lsrc = 0x41;

/* Aardvark 初始化（Master Write 模式）*/
static int aardvark_init(void) {

    int bitrate;
    /* 打开设备 */
    aardvark = aa_open(AARDVARK_PORT);
    if (aardvark == AA_PORT_NOT_FREE && aardvark < 0) {
        fprintf(stderr, "aa_open failed: %d\n", (int)aardvark);
        return -1;
    }
    if (aardvark <= 0) {
        fprintf(stderr, "aa_open returned invalid handle: %d\n", (int)aardvark);
        return -1;
    }

    // Ensure that the I2C subsystem is enabled
    aa_configure(aardvark, AA_CONFIG_SPI_I2C);

    // Enable the I2C bus pullup resistors (2.2k resistors).
    // This command is only effective on v2.0 hardware or greater.
    // The pullup resistors on the v1.02 hardware are enabled by default.
    aa_i2c_pullup(aardvark, AA_I2C_PULLUP_BOTH);

    // Enable the Aardvark adapter's power pins.
    // This command is only effective on v2.0 hardware or greater.
    // The power pins on the v1.02 hardware are not enabled by default.
    aa_target_power(aardvark, AA_TARGET_POWER_BOTH);

    // Setup the bitrate
    bitrate = aa_i2c_bitrate(aardvark, I2C_BITRATE_KHZ);
    printf("Bitrate set to %d kHz\n", bitrate);

    /* 可设置 pullups, config 等（如需要） */
    return 0;
}

static u8 crc8(u8 data)
{
    u8 crc = data;
    for (int i = 0; i < 8; i++) {
        if (crc & 0x80)
            crc = (crc << 1) ^ 0x07;   // SMBus CRC-8 polynomial
        else
            crc <<= 1;
    }
    return crc;
}
/**
 * i2c_smbus_pec - Incremental CRC8 over the given input data array
 * @crc: previous return crc8 value
 * @p: pointer to data buffer.
 * @count: number of bytes in data buffer.
 *
 * Incremental CRC8 over count bytes in the array pointed to by p
 */
static u8 i2c_smbus_pec(u8 crc, u8 *p, size_t count)
{
	int i;

	for (i = 0; i < count; i++)
		crc = crc8((crc ^ p[i]) << 8);

    printf("calc crc8 0x%x for :  ", crc);
    print_hex(p, count);
	return crc;
}


static int mctp_i2c_header_create(u8 llsrc, u8 lldst, u8 *out_buf, unsigned int mctp_len)
{
	struct mctp_i2c_hdr *hdr;

	if (mctp_len + MCTP_I2C_HDR_LEN + 1 > AARDVARK_MTU) {
        fprintf(stderr, "too big mctp_len %d", mctp_len);
        return -1;
    }

	hdr = (void *)out_buf;
	hdr->dest_slave = (lldst << 1) & 0xff;
	hdr->command = MCTP_I2C_COMMANDCODE;
	hdr->byte_count = mctp_len + 1;  /*from source slave to end of payload, not include pec byte*/
	hdr->source_slave = ((llsrc << 1) & 0xff) | 0x01;

	return sizeof(struct mctp_i2c_hdr);
}


/* 把原始 AF_MCTP 报文封装成 MCTP-over-SMBus 字节流
   说明：slave_addr 由上层传入（例如 命令行参数 / 配置）
*/
static int build_smbus_stream_from_af_mctp(const u8 *mctp_buf, size_t mctp_len, u8 *out_buf) {
    size_t pos = 0;
	
    if (!mctp_buf || mctp_len < 5 || !out_buf) {
        perror("Invalid param");
        return -1;
    }

    /*i2c link header for smbus*/
    pos = mctp_i2c_header_create(g_lsrc, g_ldevdst, out_buf, mctp_len);
    if (pos < 0) {
        perror("build i2c header fail");
        return -2;
    }

    /*copy payload for mctp packet*/
    memcpy(&out_buf[pos], mctp_buf, mctp_len);
    pos += mctp_len;

    /*pec*/
	out_buf[pos] = i2c_smbus_pec(0, (u8 *)out_buf, pos);
    pos++;
    printf("Build I2C packets: ");
    print_hex(out_buf, pos);
    return pos;
}

/* 发送并等待响应：在同一线程里完成 Master → switch to slave → read response →
 * deliver */
static int send_and_wait_response(const u8 *payload, size_t payload_len) {

    u8 slave_addr_7bit = payload[0]>>1;
    int len = payload_len - 1;
    pthread_mutex_lock(&aardvark_lock);

    if (aardvark <= 0) {
        pthread_mutex_unlock(&aardvark_lock);
        return -1;
    }

    /* The 1st byte is dst_addr, and others are input to aa_i2c_write by payload*/
    int written = aa_i2c_write(aardvark, slave_addr_7bit, AA_I2C_NO_FLAGS,
                                (u16)len, payload+1);
    if (written < 0) {
        fprintf(stderr, "aa_i2c_write error: %d\n", written);
        pthread_mutex_unlock(&aardvark_lock);
        return -1;
    }
    if ((size_t)written != len ) {
        fprintf(stderr, "aa_i2c_write wrote %d / %zu\n", written, payload_len);
        pthread_mutex_unlock(&aardvark_lock);

        return -2;
    }

    /* 切换到 slave 模式并轮询 */
    /* 设置为 slave 模式：使用 aa_i2c_slave_enable(aardvark, slave_addr) 等 */
    /* No need to set response as MCTP is UDP style transaction, no need to ACK*/
    //TODO
    int ena = aa_i2c_slave_enable(aardvark, g_lsrc, AARDVARK_MTU, AARDVARK_MTU);
    if (ena) {
        perror("slave enable fail\n");
        pthread_mutex_unlock(&aardvark_lock);
        return -3;
    }

    /* Optimized polling using aa_async_poll with exponential backoff */
    const int max_poll = 500; /* ms */
    const int poll_timeout = 50; /* Max 50ms per poll */
    int elapsed = 0;
    int got = 0;
    u8 *rxbuf = Aardvark_buf[1];
    memset(rxbuf, 0x00, AARDVARK_MTU);

    while (elapsed < max_poll && !got) {

        int poll_result = aa_async_poll(aardvark, poll_timeout);
        if (poll_result & AA_ASYNC_I2C_READ) {
            /* I2C slave data available - read it */
            int rc_read = aa_i2c_slave_read(aardvark, &rxbuf[0], AARDVARK_MTU, rxbuf+1);
            if (rc_read > 0) {
                /* Process I2C link packet */
                printf("Received I2C link packet (%d bytes):\n", rc_read);
                print_hex(rxbuf, rc_read);

                if (rxbuf[0] != g_lsrc) {
                    fprintf(stderr,"Received slave_addr 0x%x, expect 0x%x, drop and continue\n",
                           (u8)rxbuf[0], g_lsrc);
                    elapsed += poll_timeout;
                    continue;
                }

                /* Check packet has minimum length for I2C header + MCTP + PEC */
                if (rc_read < MCTP_I2C_HDR_LEN) {  /* header + at least 1 byte MCTP + PEC */
                    fprintf(stderr, "Packet too short: %d bytes\n", rc_read);
                    got = 0;
                    break;
                }

                /* Calculate PEC and verify */
                u8 calculated_pec = i2c_smbus_pec(0, rxbuf, rc_read - 1);
                u8 received_pec = rxbuf[rc_read];

                if (calculated_pec != received_pec) {
                    fprintf(stderr, "PEC verification failed: calc=0x%02x, recv=0x%02x\n",
                            calculated_pec, received_pec);
                    got = 0;
                    break;
                }
                printf("PEC verification passed\n");

                /* Extract MCTP packet (skip I2C header, exclude PEC) */
                struct mctp_i2c_hdr *i2c_hdr = (struct mctp_i2c_hdr *)rxbuf;
                int mctp_pkt_len = rc_read - (MCTP_I2C_HDR_LEN - 1) - 1;  /* total - header(not include dst) - PEC */
                u8 *mctp_packet = rxbuf + MCTP_I2C_HDR_LEN;

                /* Verify byte count matches */
                if (i2c_hdr->byte_count != mctp_pkt_len + 1) {  /* +1 for source_slave */
                    fprintf(stderr, "Byte count mismatch: hdr=%d, calc=%d\n",
                            i2c_hdr->byte_count, mctp_pkt_len + 1);
                    got = 0;
                    break;
                }

                printf("Extracted MCTP packet (%d bytes):\n", mctp_pkt_len);
                print_hex(mctp_packet, mctp_pkt_len);

                /* Deliver MCTP packet to host */
                write_to_host(mctp_packet, mctp_pkt_len);
                got = 1;
                break;
            } else if (rc_read < 0) {
                /* Other error */
                fprintf(stderr, "aa_i2c_slave_read error: %d\n", rc_read);
                got = 0;
                break;
            }
        } else if (poll_result == AA_ASYNC_NO_DATA) {
            /* No data yet - continue waiting */
            elapsed += poll_timeout;
            continue;
        } else if (poll_result < 0) {
            /* Poll error */
            fprintf(stderr, "aa_async_poll error: %d\n", poll_result);
            got = 0;
            break;
        } else {
            /* Other async events (not I2C read) */
            elapsed += poll_timeout;
        }
    }

    /* 禁用 slave（回到 master）*/
    aa_i2c_slave_disable(aardvark);
    pthread_mutex_unlock(&aardvark_lock);
    return got ? 0 : -4; /* -4 recv fail*/
}

static void dump_mctp_hdr(u8 *mctp_buf, int len) {
    struct mctp_hdr *hdr = (struct mctp_hdr *)mctp_buf;
    u8 msg_type = *(mctp_buf + sizeof(struct mctp_hdr));
    printf("mctp hdr[ver: %x, dst: %x, src:%x, flags_tag: %x], msg_type: %x, len: %x]\n",
            hdr->ver, hdr->dst, hdr->src, hdr->flags_seq_tag, msg_type, len);
}


/********************************************************************
 * Build a MCTP Control Error Response
 *
 * req_buf: received request buffer
 * req_len: length of received request
 * resp_buf: buffer to fill with response
 * cc: completion code (e.g. 0x85 ENDPOINT_UNAVAILABLE)
 ********************************************************************/
static int build_mctp_ctrl_error_resp(const u8 *req_buf, int req_len,
                               u8 *resp_buf, u8 cc)
{
    if (req_len < sizeof(struct mctp_hdr)) {

        perror("Invalid param");
        return -1;
    }

    const struct mctp_hdr *req_t = 
        (const struct mctp_hdr *)req_buf;

    const struct mctp_ctrl_hdr *req_c =
        (const struct mctp_ctrl_hdr *)(req_buf + sizeof(*req_t));

    /* ---- Fill Transport Header ---- */
    struct mctp_hdr *resp_t =
        (struct mctp_hdr *)resp_buf;
    /* ---- Fill Control Header ---- */
    struct mctp_ctrl_resp *resp_c =
        (struct mctp_ctrl_resp *)(resp_buf + sizeof(*resp_t));


    memset(resp_t, 0, sizeof(*resp_t));
    resp_t->ver = req_t->ver;
    resp_t->dst = req_t->src;     // swap direction
    resp_t->src = req_t->dst;
    resp_t->flags_seq_tag    = req_t->flags_seq_tag & 0xF7;     // same tag, TO=0 for response

    resp_c->hdr.msg_type = 0x00;             // Control Message
    resp_c->hdr.command  = req_c->command;   // same command

    /* Build Rq/D/InstanceID */
    u8 inst = req_c->rq_d_inst & 0x1F;  // keep instance ID
    resp_c->hdr.rq_d_inst =
        (0 << 7) |      // Rq=0 response
        (0 << 6) |      // D=0
        inst;

    resp_c->completion_code = cc;

    return sizeof(*resp_t) + sizeof(*resp_c);
}

int i2c_handle_req_from_host(u8 *mctp_buf, int len) {
    int ret;
    u8 *sndbuf = Aardvark_buf[0];
    u8 *rcvbuf = Aardvark_buf[1];

    dump_mctp_hdr(mctp_buf, len);

    memset(sndbuf, 0x00, AARDVARK_MTU);

    ret  = build_smbus_stream_from_af_mctp(mctp_buf, len, sndbuf);
    if (ret > 0) {
        ret = send_and_wait_response(sndbuf, ret);
    }

    if (ret < 0) {
        //TODO send error response to host
        ret = build_mctp_ctrl_error_resp(mctp_buf, len, rcvbuf, MCTP_CC_ERROR);
        if (ret >0) {
            printf("Snd MCTP Ctrl Erro resp\n");
            write_to_host(rcvbuf, ret);
        }
    }

    return ret;
}
int i2c_proxy_init() {
    if (aardvark_init() != 0) {
        fprintf(stderr, "aardvark init fail\n");
        return -1;
    }

    return 0;
}

void i2c_proxy_close() {
    /* 清理 */
    if (aardvark > 0) {
        aa_close(aardvark);
    }
}

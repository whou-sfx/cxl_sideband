/* aardvark_fake.c - Fake Aardvark implementation for testing */
#include "aardvark.h"
#include "mctp.h"
#include "demo_main.h"
#include <stdio.h>
#include <string.h>

typedef unsigned char u8;

/* Fake Aardvark handle counter */
static int fake_handle_counter = 1;

/* Status codes used in our code */
#define AA_PORT_NOT_FREE 0x8000
#define AA_CONFIG_SPI_I2C 0x03
#define AA_I2C_PULLUP_BOTH 0x03
#define AA_TARGET_POWER_BOTH 0x03
#define AA_I2C_NO_FLAGS 0x00

/* Constants */
#define MCTP_I2C_COMMANDCODE 0x0F
#define PLDM_TYPE_BASE 0x00
#define PLDM_CMD_GET_TID 0x02
#define PLDM_CMD_GET_PLDM_VERSION 0x03
#define PLDM_CC_SUCCESS 0x00

/* Global buffer for simulated slave response */
static u8 g_fake_resp[4096];

/* PLDM Header structure (copied from pldm.c) */
#pragma pack(push, 1)
struct pldm_header {
    unsigned char instance_id : 5;   // bits 0–4
    unsigned char rsvd        : 1;   // bit 5
    unsigned char D           : 1;   // bit 6
    unsigned char rq          : 1;   // bit 7
    unsigned char type        : 6;   // bits 0–5
    unsigned char hdr_ver     : 2;   // bits 6–7
    unsigned char command;
};
#pragma pack(pop)

/* Build MCTP I2C header */
static int build_i2c_header(u8 llsrc, u8 lldst, u8 *out_buf, unsigned int mctp_len)
{
    struct mctp_i2c_hdr *hdr;
    if (mctp_len + 5 > 4096) { // sizeof(mctp_i2c_hdr) + 1 byte
        fprintf(stderr, "too big mctp_len %d\n", mctp_len);
        return -1;
    }
    hdr = (void *)out_buf;
    hdr->dest_slave = (lldst << 1) & 0xff;
    hdr->command = MCTP_I2C_COMMANDCODE;
    hdr->byte_count = mctp_len + 1;  /* from source slave to end of payload, not include pec byte */
    hdr->source_slave = ((llsrc << 1) & 0xff) | 0x01;
    return sizeof(struct mctp_i2c_hdr);
}

/* Simulate PLDM response generation (similar to pldm.c) */
static int handle_pldm_request(const unsigned char *mctp_buf, int len, u8 *resp_buf)
{
    struct mctp_hdr *mctp_req_hdr;
    struct pldm_header *req;
    struct mctp_hdr *mctp_hdr_resp;
    struct pldm_header *resp;
    unsigned char *resp_payload;
    int resp_len;

    if (len < sizeof(struct mctp_hdr) + 1 + sizeof(struct pldm_header)) {
        printf("Error: buffer too small\n");
        return -1;
    }

    mctp_req_hdr = (struct mctp_hdr *)mctp_buf;
    req = (struct pldm_header *)(mctp_buf + sizeof(struct mctp_hdr) + 1);

    /* Build MCTP header */
    mctp_hdr_resp = (struct mctp_hdr *)resp_buf;
    resp = (struct pldm_header *)(resp_buf + sizeof(struct mctp_hdr) + 1);
    resp_payload = resp_buf + sizeof(struct mctp_hdr) + 1 + sizeof(struct pldm_header);
    resp_len = sizeof(struct mctp_hdr) + 1 + sizeof(struct pldm_header);

    /* Copy and adjust MCTP header */
    memcpy(resp_buf, mctp_buf, sizeof(struct mctp_hdr) + 1);
    mctp_hdr_resp->dst = mctp_req_hdr->src;
    mctp_hdr_resp->src = mctp_req_hdr->dst;
    mctp_hdr_resp->flags_seq_tag &= mctp_req_hdr->flags_seq_tag & 0xF7; /* clear TO bit */

    /* Build PLDM response header */
    memcpy(resp, req, sizeof(struct pldm_header));
    resp->rq = 0; // Response

    /* Generate PLDM response based on command */
    switch (req->command) {
    case PLDM_CMD_GET_TID:
        resp_payload[0] = PLDM_CC_SUCCESS;
        resp_payload[1] = 0x42; // TID
        resp_len += 2;
        break;

    case PLDM_CMD_GET_PLDM_VERSION:
        resp_payload[0] = PLDM_CC_SUCCESS;
        resp_payload[1] = 0x00; // major
        resp_payload[2] = 0x00; // minor
        resp_payload[3] = 0x00; // update
        resp_payload[4] = 0x01; // a
        resp_len += 10;
        break;

    default:
        resp_payload[0] = 0x01; // PLDM_CC_ERROR_UNSUPPORTED_CMD
        resp_len += 1;
        break;
    }
    printf("PLDM RESP Packet (%d): ", resp_len);
    print_hex(resp_buf, resp_len);
    return resp_len;
}

/* Essential Aardvark functions used in i2c_proxy_aardvark.c */
Aardvark aa_open(int port_number)
{
    printf("FAKE: aa_open called for port %d\n", port_number);
    if (port_number == AA_PORT_NOT_FREE) {
        return AA_PORT_NOT_FREE;
    }
    return fake_handle_counter++;
}

int aa_configure(Aardvark aardvark, AardvarkConfig config)
{
    printf("FAKE: aa_configure called with config 0x%x\n", config);
    if (aardvark <= 0) {
        return AA_INVALID_HANDLE;
    }
    return AA_OK;
}

int aa_i2c_pullup(Aardvark aardvark, u08 pullup_mask)
{
    printf("FAKE: aa_i2c_pullup called with mask 0x%x\n", pullup_mask);
    if (aardvark <= 0) {
        return AA_INVALID_HANDLE;
    }
    return AA_OK;
}

int aa_target_power(Aardvark aardvark, u08 power_mask)
{
    printf("FAKE: aa_target_power called with mask 0x%x\n", power_mask);
    if (aardvark <= 0) {
        return AA_INVALID_HANDLE;
    }
    return AA_OK;
}

int aa_i2c_bitrate(Aardvark aardvark, int bitrate_khz)
{
    printf("FAKE: aa_i2c_bitrate called: %d kHz\n", bitrate_khz);
    if (aardvark <= 0) {
        return AA_INVALID_HANDLE;
    }
    return bitrate_khz;
}

int aa_i2c_write(Aardvark aardvark, u16 slave_addr, AardvarkI2cFlags flags,
                 u16 num_bytes, const u08 *data_out)
{
    printf("FAKE: aa_i2c_write called: addr=0x%x, bytes=%d\n", slave_addr, num_bytes);
    if (aardvark <= 0) {
        return AA_INVALID_HANDLE;
    }
    if (num_bytes > 0 && data_out) {
        printf("FAKE: Writing data: ");
        for (int i = 0; i < (num_bytes > 16 ? 16 : num_bytes); i++) {
            printf("%02x ", data_out[i]);
        }
        printf("%s\n", num_bytes > 16 ? "..." : "");

        /* Store the write data in global buffer for slave_read to process */
        if (num_bytes <= sizeof(g_fake_resp)) {
            memcpy(g_fake_resp, data_out-1, num_bytes+1);
            printf("FAKE: Stored %d bytes for slave processing\n", num_bytes+1);
        } else {
            printf("FAKE: Warning: Write data too large (%d > %zu), truncating\n",
                   num_bytes, sizeof(g_fake_resp));
            memcpy(g_fake_resp, data_out-1, sizeof(g_fake_resp));
        }
    }
    return num_bytes;
}

int aa_i2c_slave_enable(Aardvark aardvark, u08 addr, u16 maxTxBytes, u16 maxRxBytes)
{
    printf("FAKE: aa_i2c_slave_enable called: addr=0x%x, maxTx=%d, maxRx=%d\n",
           addr, maxTxBytes, maxRxBytes);
    if (aardvark <= 0) {
        return AA_INVALID_HANDLE;
    }
    return AA_OK;
}
int aa_async_poll (Aardvark aardvark, int timeout)
{
    return AA_ASYNC_I2C_READ;
}

int aa_i2c_slave_read(Aardvark aardvark, u08 *addr, u16 num_bytes, u08 *data_in)
{
    printf("FAKE: aa_i2c_slave_read called\n");
    if (aardvark <= 0) {
        return AA_INVALID_HANDLE;
    }

     if (data_in && num_bytes > 0) {
        /* Simulate receiving I2C write and generating PLDM response */
        printf("FAKE: Simulating slave response generation\n");

        /* Extract MCTP packet from the I2C write data (stored in global buffer) */
        struct mctp_i2c_hdr *i2c_hdr = (struct mctp_i2c_hdr *)g_fake_resp;
        int total_len = i2c_hdr->byte_count + 4; /* include header and pec */
        int mctp_len = i2c_hdr->byte_count - 1;  /* from source_slave to end of payload */

        printf("FAKE: I2C write had %d bytes total, MCTP packet %d bytes\n", total_len, mctp_len);

        /* swtich link src/dst address (remove R/W bit) */
        u8 source_slave = (i2c_hdr->dest_slave >> 1) & 0x7F;
        u8 dst_slave = (i2c_hdr->source_slave >> 1) & 0x7F;

        /* Parse the MCTP packet (skip I2C header) */
        u8 *mctp_packet = g_fake_resp + sizeof(struct mctp_i2c_hdr);

        /* Generate PLDM response */
        u8 resp_mctp[256];
        int resp_mctp_len = handle_pldm_request(mctp_packet, mctp_len, resp_mctp);

        if (addr) {
                *addr = dst_slave;
            }

        if (resp_mctp_len > 0) {
            /* Build I2C response with header and PEC */
            u8 i2c_resp[1024];
            int pos = build_i2c_header(source_slave, dst_slave, i2c_resp, resp_mctp_len);

            memcpy(i2c_resp + pos, resp_mctp, resp_mctp_len);
            pos += resp_mctp_len;

            /* Add PEC */
            i2c_resp[pos] = i2c_smbus_pec(0, i2c_resp, pos);
            pos++;

            printf("FAKE: Generated I2C response of %d bytes\n", pos);
            print_hex(i2c_resp, pos);

            /* Copy response to output buffer, skip the 1st dst addr byte */
            int copy_bytes = (num_bytes > pos -1 ) ? pos-1 : num_bytes;
            memcpy(data_in, i2c_resp + 1, copy_bytes);
            return copy_bytes;
        }
    }

    return 0;
}

int aa_i2c_slave_disable(Aardvark aardvark)
{
    printf("FAKE: aa_i2c_slave_disable called\n");
    if (aardvark <= 0) {
        return AA_INVALID_HANDLE;
    }
    return AA_OK;
}

int aa_close(Aardvark aardvark)
{
    printf("FAKE: aa_close called for handle %d\n", aardvark);
    if (aardvark <= 0) {
        return AA_INVALID_HANDLE;
    }
    return AA_OK;
}

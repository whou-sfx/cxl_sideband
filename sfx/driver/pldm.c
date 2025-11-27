#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "mctp.h"

#define PLDM_TYPE_BASE          0x00
#define PLDM_CMD_GET_TID        0x02
#define PLDM_CMD_GET_PLDM_VERSION 0x03

#define PLDM_CC_SUCCESS         0x00

#pragma pack(push, 1)
struct pldm_header {
    // Byte 0
    uint8_t instance_id      : 5;   // bits 0–4
    uint8_t rsvd             : 1;   // bits 6
    uint8_t D                : 1;   // bit 6
    uint8_t rq               : 1;   // bit 7

    // Byte 1
    uint8_t type             : 6;   // bits 2–5
    uint8_t hdr_ver          : 2;   // bits 6–7

    // Byte 2
    uint8_t command;

};
#pragma pack(pop)

static void print_hex(const uint8_t *buf, int len)
{
    for (int i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\n");
}

// 构建PLDM Response Header
static void build_pldm_resp_hdr(struct pldm_header *resp,
                                const struct pldm_header *req)
{
    // Response: Rq/D bit 为 0（响应），InstanceID 保持不变
    memcpy(resp, req, sizeof(struct pldm_header));
    resp->rq  = 0;
    /** printf(" Req: HdrVer=%d, Type=0x%02X, Cmd=0x%02X\n", */
    /**        req->hdr_ver, req->type, req->command); */
    /** printf(" Res: HdrVer=%d, Type=0x%02X, Cmd=0x%02X\n", */
    /**        resp->hdr_ver, resp->type, resp->command); */
 
}

// main handler
int handle_pldm_req(const uint8_t *mctp_buf, int len)
{
    struct mctp_hdr *mctp_req_hdr;
    unsigned char *buf;
    struct pldm_header *req;

    uint8_t resp_buf[256];
    struct mctp_hdr *mctp_hdr_resp = (struct mctp_hdr *)resp_buf;
    struct pldm_header *resp = (struct pldm_header *)(resp_buf + sizeof(struct mctp_hdr) + 1);
    uint8_t *resp_payload = (resp_buf + sizeof(struct mctp_hdr) + 1 + sizeof(struct pldm_header));
    int resp_len = sizeof(struct mctp_hdr) + 1 + sizeof(struct pldm_header);
 
 
    if (len < sizeof(struct pldm_header) + sizeof(struct mctp_hdr) + 1) {
        printf("Error: buffer too small\n");
        return -1;
    }
        
    mctp_req_hdr = (struct mctp_hdr *)mctp_buf;
    buf = mctp_buf + sizeof(struct mctp_hdr) + 1;


    // 1. 解析 Request Header
    req = (struct pldm_header *)buf;
    const uint8_t *payload = buf + sizeof(struct pldm_header);
    int payload_len = len - sizeof(struct mctp_hdr) - 1 - sizeof(struct pldm_header);


    printf("=== PLDM Request ===\n");
    print_hex(buf,len - sizeof(struct mctp_hdr) - 1);
    printf(" Rq=%d, InstanceID=%d\n", req->rq, req->instance_id);
    printf(" HdrVer=%d, Type=0x%02X, Cmd=0x%02X\n",
           req->hdr_ver, req->type, req->command);
    printf(" Payload (%d bytes): ", payload_len);
    print_hex(payload, payload_len);

    // 2. 构建 Response
    //build resp mctp_hdr
    memcpy(resp_buf, mctp_buf, sizeof(struct mctp_hdr)+1);
    mctp_hdr_resp->dst = mctp_req_hdr->src;
    mctp_hdr_resp->src = mctp_req_hdr->dst;
    mctp_hdr_resp->flags_seq_tag &=mctp_req_hdr->flags_seq_tag & 0xF7; /*clear the TO bit*/


    build_pldm_resp_hdr(resp, req);

   
    // 根据 command code 返回不同响应
    switch (req->command) {

    case PLDM_CMD_GET_TID:
        printf(" Handle: GET_TID\n");
        resp_payload[0] = PLDM_CC_SUCCESS;   // completion code
        resp_payload[1] = 0x42;              // TID 示例
        resp_len += 2;
        break;

    case PLDM_CMD_GET_PLDM_VERSION:
        printf(" Handle: GET_PLDM_VERSION\n");
        resp_payload[0] = PLDM_CC_SUCCESS;
        // 固定版本号 返回 1.0.0
        resp_payload[1] = 0x00; // major
        resp_payload[2] = 0x00; // minor
        resp_payload[3] = 0x00; // update
        resp_payload[4] = 0x01; // a
        resp_payload[5] = 0x00; // cnt
        resp_payload[6] = 0x01;
        resp_payload[7] = 0x01;
        resp_payload[8] = 0x01;
        resp_payload[9] = 0x01;

        resp_len += 10;
        break;

    default:
        printf(" Handle: UNKNOWN CMD\n");
        resp_payload[0] = 0x01;  // PLDM_CC_ERROR_UNSUPPORTED_CMD
        resp_len += 1;
        break;
    }

    // 3. 打印 Response 关键字段
    printf("\n=== PLDM Response ===\n");
    printf(" InstanceID=%d\n", resp->instance_id);
    printf(" Type=0x%02X Cmd=0x%02X CC=0x%02X\n",
           resp->type, resp->command, resp_payload[0]);

    printf(" Response Buffer (%d bytes): ", resp_len);
    print_hex(resp, resp_len - sizeof(struct mctp_hdr) - 1);

    write_to_host(resp_buf, resp_len);

    return resp_len;
}


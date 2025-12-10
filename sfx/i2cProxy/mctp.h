/*************************************************************************
@File Name: mctp.h
@Desc: 
@Author: Andy-wei.hou
@Mail: wei.hou@scaleflux.com 
@Created Time: 2025年11月27日 星期四 11时36分37秒
@Log: 
************************************************************************/
#ifndef __MCTP_H__
#define __MCTP_H__

#pragma pack(push, 1)
/* MCTP packet definitions */
struct mctp_hdr {
    uint8_t  ver;
    uint8_t  dst;                                                                                                                 
    uint8_t  src;
    uint8_t  flags_seq_tag;
};

/* MCTP Control Message Header */
struct mctp_ctrl_hdr {
    uint8_t msg_type;       // 0x00 for Control Message
    uint8_t rq_d_inst;      // rq/D/Instance ID
    uint8_t command;        // ctrl command code
};

/* Response Format: hdr + completion code */
struct mctp_ctrl_resp {
    struct mctp_ctrl_hdr hdr;
    uint8_t completion_code;
};

/* Header on the wire i2c */
struct mctp_i2c_hdr {
	uint8_t dest_slave;
	uint8_t command;
	/* Count of bytes following byte_count, excluding PEC */
	uint8_t byte_count;
	uint8_t source_slave;
};

#pragma pack(pop)


#define MSG_TYPE_PLDM 1
#define MSG_TYPE_CXLCCI  8

enum {
    MTCP_CC_SUCC = 0x00,
    MCTP_CC_ERROR = 0x01,

};

uint8_t i2c_smbus_pec(uint8_t crc, uint8_t *p, int count);

int write_to_host(uint8_t* out_buf, int len);
#endif

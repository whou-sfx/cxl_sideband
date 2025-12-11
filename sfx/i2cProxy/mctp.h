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

#include <stdint.h>

#pragma pack(push, 1)
/* MCTP packet definitions */
struct mctp_hdr {
    unsigned char  ver;
    unsigned char  dst;                                                                                                                 
    unsigned char  src;
    unsigned char  flags_seq_tag;
};

/* MCTP Control Message Header */
struct mctp_ctrl_hdr {
    unsigned char msg_type;       // 0x00 for Control Message
    unsigned char rq_d_inst;      // rq/D/Instance ID
    unsigned char command;        // ctrl command code
};

/* Response Format: hdr + completion code */
struct mctp_ctrl_resp {
    struct mctp_ctrl_hdr hdr;
    unsigned char completion_code;
};

/* Header on the wire i2c */
struct mctp_i2c_hdr {
	unsigned char dest_slave;
	unsigned char command;
	/* Count of bytes following byte_count, excluding PEC */
	unsigned char byte_count;
	unsigned char source_slave;
};

#pragma pack(pop)


#define MSG_TYPE_PLDM 1
#define MSG_TYPE_CXLCCI  8

enum {
    MTCP_CC_SUCC = 0x00,
    MCTP_CC_ERROR = 0x01,

};

unsigned char i2c_smbus_pec(unsigned char crc, unsigned char *p, int count);

int write_to_host(unsigned char* out_buf, int len);
#endif

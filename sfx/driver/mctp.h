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
/* MCTP packet definitions */
struct mctp_hdr {
    uint8_t  ver;
    uint8_t  dst;                                                                                                                 
    uint8_t  src;
    uint8_t  flags_seq_tag;
};

#define MSG_TYPE_PLDM 1
#define MSG_TYPE_CXLCCI  8


int write_to_host(uint8_t* out_buf, int len);
#endif

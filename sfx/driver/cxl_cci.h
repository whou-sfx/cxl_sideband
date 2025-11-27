/*************************************************************************
@File Name: cxl_cci.h
@Desc: 
@Author: Andy-wei.hou
@Mail: wei.hou@scaleflux.com 
@Created Time: 2025年11月27日 星期四 20时03分51秒
@Log: 
************************************************************************/
#ifndef __CXL_CCI_H__
#define __CXL_CCI_H__
/* CXL r3.1 Figure 7-19: CCI Message Format */
struct cxlmi_cci_msg {
	uint8_t category;
	uint8_t tag;
	uint8_t rsv1;
	uint8_t command;
	uint8_t command_set;
	uint8_t pl_length[3]; /* 20 bit little endian, BO bit at bit 23 */
	uint16_t return_code;
	uint16_t vendor_ext_status;
	uint8_t payload[];
} __attribute__ ((packed));

/* CXL r3.1 Section 8.2.9.1.1: Identify (Opcode 0001h) */
struct cxlmi_cmd_identify_rsp {
	uint16_t vendor_id;
	uint16_t device_id;
	uint16_t subsys_vendor_id;
	uint16_t subsys_id;
	uint64_t serial_num;
	uint8_t max_msg_size;
	uint8_t component_type;
} __attribute__((packed));


#endif

#ifndef __I2C_PROXY_AARDVARK_H__
#define __I2C_PROXY_AARDVARK_H__

typedef unsigned char u8;
int i2c_proxy_init(void);

void i2c_proxy_close(void);

int i2c_handle_req_from_host(u8 *mctp_buf, int len);

#endif

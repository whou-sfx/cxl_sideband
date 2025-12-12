#ifndef __I2C_PROXY_AARDVARK_H__
#define __I2C_PROXY_AARDVARK_H__

typedef unsigned char u8;

struct i2c_proxy_params {
    u8 haddr;          /* g_lsrc address */
    u8 daddr;          /* g_ldevdst address */
    int freq_khz;      /* I2C bitrate in kHz */
    int timeout_ms;    /* slave polling timeout in ms */
};

int i2c_proxy_init(const struct i2c_proxy_params *params);
int i2c_proxy_init_default(void); /* for backward compatibility */

void i2c_proxy_close(void);

int i2c_handle_req_from_host(u8 *mctp_buf, int len);


#endif

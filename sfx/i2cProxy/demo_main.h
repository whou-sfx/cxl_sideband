#ifndef __DEMO_MAIN_H
#define __DEMO_MAIN_H

void print_hex(const uint8_t *buf, int len);

int write_to_host(uint8_t *mctp_buf, int len);
#endif

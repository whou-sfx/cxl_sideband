#ifndef UTIL_H
#define UTIL_H

#include <stdint.h>
#include <stdio.h>

static inline void print_hex(const uint8_t *buf, int len)
{
    for (int i = 0; i < len; i++) {
        printf("%02X ", buf[i]);
        if (i % 16 == 15)
            printf("\n");
    }
    if (len % 16 != 0)
        printf("\n");
}

static inline int parse_hex_string(const char *input, uint8_t *output, int max_len)
{
    int count = 0;
    const char *p = input;

    while (*p && count < max_len) {
        while (*p == ' ' || *p == '\t' || *p == '\n')
            p++;

        if (!*p)
            break;

        unsigned int byte;
        if (sscanf(p, "%02x", &byte) != 1)
            return -1;

        output[count++] = (uint8_t)byte;

        while (*p && *p != ' ')
            p++;
    }
    return count;
}

#endif

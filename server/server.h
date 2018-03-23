/*
 * udp.h - UDP Packet Generator Common
 */

#ifndef UDP_H
#define UDP_H

#include <stdint.h>

#define UDPPORT 3425
#define PAYLOADSIZE 128

typedef struct {
    uint32_t seq;
    uint64_t tsctx;
    uint64_t tscrx;
    char payload[PAYLOADSIZE];
}__attribute__((packed)) udpdata_t;

typedef struct {
    char message;
    uint64_t seq; // номер пакета, который будет перезапрошен
    char payload[PAYLOADSIZE];
} datatx_t;

__inline__ unsigned long long rdtsc(void)
{
    unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

#endif

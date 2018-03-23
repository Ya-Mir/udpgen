/*
 * client.h - UDP Packet Generator Common
 *
 */


#include <stdint.h>

#define UDPPORT 3425  //используем не привилегированный порт
#define PAYLOADSIZE 128 //размер полезной нагрузки

typedef struct  {
    uint32_t seq;
    uint64_t tsctx;
    uint64_t tscrx;
    char payload[PAYLOADSIZE];
} udpdata_t ;

typedef struct {
    char message;
    uint64_t seq; // номер пакета, который будет перезапрошен
    char payload[PAYLOADSIZE];
} udpretransmission_t;

unsigned long long rdtsc();

unsigned long long get_hz();

__inline__ unsigned long long rdtsc(void)
{
    unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

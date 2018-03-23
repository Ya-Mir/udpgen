/*
 * udp.h - UDP Packet Generator Common
 *
 * Copyright (C) 2014 Citrix
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
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

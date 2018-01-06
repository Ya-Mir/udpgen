/*
 * udprx.c - UDP Packet Generator Receiver
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

#include <stdio.h>
#include <inttypes.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include "udp.h"


udpdata_t *udpdata;
int       npkts = 1024; //разбер буфера под пакеты
int recv_pkt = 0;
int drop_pkt = 0;
int       bufsz = sizeof(udpdata_t);

int       toler_sec      = 400; /* number of seconds to wait for each packet */
int       ignore_sigalrm = 0;
int       suppress_dump  = 1;
char *reask = "reask"; 
struct itimerval itv;

unsigned long long get_hz(void){
    FILE               *fp;
    char                buf[1024];
    unsigned long long  ret = 0;
    float               f = 0;

    if ((fp = fopen("/proc/cpuinfo", "r")) == NULL){
        perror("fopen");
        goto err;
    }

    while(fgets(buf, sizeof(buf), fp)){
        if (strncmp(buf, "cpu MHz", 7)){
            continue;
        }
        sscanf(buf, "cpu MHz%*[ \t]: %f\n", &f);
        break;
    }
    
    ret = (unsigned long long)(f*1000000.0);

out:
    if (fp)
        fclose(fp);

    return(ret);

err:
    ret = 0;
    goto out;
}



__inline__ unsigned long long rdtsc(void)
{
    unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}


void timer_disable() {
    ignore_sigalrm = 1;
}

void timer_reset() {
    if (setitimer(ITIMER_REAL, &itv, NULL))
        perror("setitimer");
}

void timer_init() {
    struct timeval tv;
    tv.tv_sec  = toler_sec;
    tv.tv_usec = 0;
    itv.it_value    = tv;
    itv.it_interval = tv;
}

void dump(void){
    int                i;

    unsigned long long hz = get_hz();
    int                last_recv_pkt = -1;
    int                total_pkts_dropped = npkts;
    float              tput_mbs;

    timer_disable();

    for (i=0; i<npkts; i++){
        if (!suppress_dump)
            printf("%5u %" PRIu64 " %" PRIu64 " (%" PRIu64 ") %llu\n",
                   udpdata[i].seq, udpdata[i].tsctx,
                   udpdata[i].tscrx, udpdata[i].tscrx-udpdata[i].tsctx,
                   (udpdata[i].tscrx-udpdata[i].tsctx)*1000000/hz);

        if(udpdata[i].tsctx != 0) {
            last_recv_pkt = i;
            --total_pkts_dropped;
        }
    }

    // Print throughput
    if (last_recv_pkt >= 0) {
        tput_mbs = (long long)(npkts - total_pkts_dropped) * bufsz /
            ((udpdata[last_recv_pkt].tscrx - udpdata[0].tsctx)*1.0/hz) /
            1000000.0;
    } else {
        tput_mbs = 0.0;
    }

   // fprintf(stderr, "Average throughput: %.0f MB/s = %.2f Gbit/s\r",
   // tput_mbs, tput_mbs * 8 / 1000);

   // Print number of dropped packets
   fprintf(stderr, "Drop packets: %d, recived packet %d\r",
   drop_pkt, recv_pkt);

   //printf(" %-4s| %-10s| %-5s|\r", "ID", "NAME", "AGE");
    //system("clear");
}

void sigterm_h(int signal){
   dump();
   exit(0);
}

void sigalrm_h(int signal){
    if (!ignore_sigalrm) {
        fprintf(stderr, "Warning: Received no further packets in %u seconds\n", toler_sec);
        dump();
        exit(0);
    }
}

int main() {
    // Local variables
    struct sockaddr_in our_addr;
    void               *buf  = NULL;
    int                sock  = -1;
    unsigned int       slen  = 0;
    int                err   = 0;
    int            bytesize  = 0;
    int                i     = 0;

    // Initialise timer
    timer_init();

    // Allocate receive buffer
    if ((udpdata = (udpdata_t *)calloc(npkts, sizeof(*udpdata))) == NULL)
    {
        perror("calloc");
        goto err;
    }
    if ((buf = malloc(bufsz)) == NULL){
        perror("malloc");
        goto err;
    }

    // Handles kill
    signal(SIGINT, sigterm_h);

    // Handles timer
    signal(SIGALRM, sigalrm_h);

    // Setup UDP socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
        perror("socket");
        fprintf(stderr, "Error creating temporary socket.\n");
        goto err;
    }
    memset(&our_addr, 0, sizeof(our_addr));
    our_addr.sin_family      = AF_INET;
    our_addr.sin_addr.s_addr = INADDR_ANY;
    our_addr.sin_port        = htons(UDPPORT);
    if (bind(sock, (struct sockaddr *)&our_addr, sizeof(struct sockaddr)) < 0){
        perror("bind");
        goto err;
    }

    // Receive packets
    //for (i=0; i<npkts; i++){
    for (;;) {
    slen = sizeof(struct sockaddr);
        bytesize = recvfrom(sock, buf, bufsz, 0, (struct sockaddr *)&our_addr, &slen);
        if (bytesize < 0){
            perror("recvfrom");
            goto err;
        } else
        if (bytesize != bufsz){
            fprintf(stdout, "Received unknown packet.\n");
            goto err;
        } else
            if (bytesize == bufsz) {
		//приняли пакет правильного размера 
		recv_pkt++;
                memcpy(&udpdata[i], buf, sizeof(udpdata_t));
                udpdata[i].tscrx = rdtsc();
                timer_reset();
                //детектилка дропов 
		if (udpdata[i].seq != recv_pkt) { 
		drop_pkt = udpdata[i].seq - recv_pkt;
		//тут перезапрашиваеи пакетитки 
		strcpy(buf,reask);
		sendto(sock,buf,strlen(buf),0,&from,len);


		}
		i++;
                if (i > 1024) i=0;
                
//		printf ("recive pkt %d", i);
        // Dump
        dump();
            }
    }

out:
    
    if (sock >= 0)
        close(sock);
    if (buf)
        free(buf);

    // Return
    return(err);

err:
    err = 1;
    goto out;
}


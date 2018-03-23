 /*
 * udprx.c - UDP Packet Generator Server
 *
 * TODO:
 * выкинуть все не нужные таймеры
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
#include <curses.h>


udpdata_t *datarx;
datatx_t *datatx;
#define       npkts 1024 //размер буфера под пакеты
int       recv_pkt = 0; //счетчик принятых пакетов правильного размера
unsigned int       drop_pkt = 0;
int       bufszrx = sizeof(udpdata_t); //размер буфера приема
int       bufsztx = sizeof(datatx_t);
int       toler_sec      = 400; /* number of seconds to wait for each packet */
int       ignore_sigalrm = 0;
int       suppress_dump  = 1;
int       pktCount = 0;
char *re_ask = "rerequest";
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
    unsigned long long hz = get_hz();
    int                last_recv_pkt = -1;   
    float              tput_mbs;
       timer_disable();
       last_recv_pkt = pktCount;
    // Print throughput
    if (recv_pkt >= 0) {
        tput_mbs = (long long)(recv_pkt - drop_pkt) * bufszrx /
            ((datarx[last_recv_pkt].tscrx - datarx[0].tsctx)*1.0/hz) /
            1000000.0;
    } else {
        tput_mbs = 0.0;
    }
    mvprintw(0,0, "Drop packets: %d, recived packet %d\r",drop_pkt, recv_pkt);
    mvprintw(1,0, "Average throughput: %.0f MB/s = %.2f Gbit/s\r", tput_mbs, tput_mbs * 8 / 1000);
   // mvprintw(2,0, "tsc rx %u %u", datarx[last_recv_pkt].tscrx, datarx[0].tsctx );
    refresh();

}

void sigterm_h(int signal){
   fprintf(stdout, "aplication recived signal%d \n", signal);
   exit(0);
}

void sigalrm_h(int signal){
fprintf(stdout, "aplication recived signal%d \n", signal);
        exit(0);
}

int main() {
    //------ncurses init----------------------
    WINDOW *w = initscr();
    noecho();
    cbreak();
    nodelay(w, TRUE);
    if(has_colors() == FALSE)
    {	endwin();
            fprintf(stdout,"Your terminal does not support color\n");
            exit(1);
    }
    start_color();
    // -------------end ncurses---------------

    // Local variables
    struct sockaddr_in our_addr;
    void               *bufrx  = NULL;
    void               *buftx  = NULL;
    int                sock  = -1;
    //unsigned int       slen  = 0;
    int                err   = 0;
    int            bytesize  = 0;
    //счетчик принятых пакетов
    int                i     = 0;

    int bufferisfull = 0;
    struct timeval timeout_select;
    // Initialise timer
    timer_init();
    timeout_select.tv_sec = 1;
    timeout_select.tv_usec = 0;

    // Allocate receive buffer
    if ((datarx = (udpdata_t *)calloc(npkts, sizeof(*datarx))) == NULL)
    {
        perror("calloc");
        goto err;
    }
    if ((bufrx = malloc(bufszrx)) == NULL){
        perror("malloc");
        goto err;
    }

    // Allocate tx buffer
    if ((datatx = (datatx_t *)calloc(npkts, sizeof(*datatx))) == NULL)
    {
        perror("calloc");
        goto err;
    }
    if ((buftx = malloc(bufsztx)) == NULL){
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
    // settings for select()
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);

    fd_set writefds;
    FD_ZERO(&writefds);
    FD_SET(sock, &writefds);

    fd_set except;
    FD_ZERO(&except);
    FD_SET(sock, &except);

    struct sockaddr from;
    unsigned int len=sizeof(from);

    // Receive packets
    for (;;) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        if (select(sock+1, &readfds, &writefds, NULL,&timeout_select) == -1) {
            perror("select");
            exit(4);
        }


        if (FD_ISSET(sock, &readfds)) {
        bytesize = recvfrom(sock, bufrx, bufszrx, 0,&from, &len);
        if (bytesize < 0){
            perror("recvfrom");
            goto err;
        } else
        if (bytesize != bufszrx){
            //fprintf(stdout, "Received unknown packet.\n");
            mvprintw (5,0, "Received unknown size packet");
            refresh();
            goto err;
        } else
            //приняли пакет правильного размера
            if (bytesize == bufszrx) {
                recv_pkt++;
                pktCount = i;
                memcpy(&datarx[i], bufrx, sizeof(udpdata_t));
                datarx[i].tscrx = rdtsc();
                timer_reset();

                //обнулим флаг конца буфера
                if (i > 1 && bufferisfull ) bufferisfull = 0;

                //Проверяем нумерацию пакетов.
                //если достигли конца массива, то сравниваем последний элемент с первым,
                //инчае по порядку
                int n = bufferisfull ? 1024 : i;

                if (datarx[i].seq == 0) {
                drop_pkt = 0;
                }
                //если принятых пакетов больше чем два то считаем потери
                if (recv_pkt > 2 && datarx[i].seq - datarx[n - 1].seq > 1)
                {
                drop_pkt = datarx[i].seq - recv_pkt;
                //перезапрашиваем пакетики
                //заполняем структуру для udp пакета
                ((datatx_t *)buftx)->seq = datarx[i].seq;
                sendto(sock,buftx,sizeof(datatx_t),0,&from,len);
                mvprintw(6,0,"DEBUG:  packet resend seq: %d",datarx[i].seq);
                }

                i++;
                //TODO сделать проверку на размер массива
                if (i > 1023) {
                    i = 1;
                    bufferisfull = 1;
                }

            }
    }
        dump();
    }

out:
    
    if (sock >= 0)
        close(sock);
    if (bufrx)
        free(bufrx);
    if (buftx)
        free(buftx);
    return(err);

err:
    err = 1;
    goto out;
}


 /*
 * udprx.c - UDP Packet Generator Server
 *
 * TODO:
 * выкинуть все не нужные таймеры
 *
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


#define Mbps(bytes, usec) ((bytes) * 8. / (usec))
#define       npkts 1024 //размер буфера под пакеты

udpdata_t *datarx;
datatx_t *datatx;
unsigned int       recv_pkt = 0; //счетчик принятых пакетов правильного размера
unsigned int       drop_pkt = 0;
int       bufszrx = sizeof(udpdata_t); //размер буфера приема
int       bufsztx = sizeof(datatx_t);
int       toler_sec      = 400; /* number of seconds to wait for each packet */
int       ignore_sigalrm = 0;
int       suppress_dump  = 1;
int       pktCount = 0;
char *re_ask = "rerequest";
struct itimerval itv;
double throughput = 0;

void dump(void){
    mvprintw(1,0, "Drop packets: %d, recived packet %d\r",drop_pkt, recv_pkt);
    mvprintw(2,0,"BANDWIDTH: %.2f Mbit/s ",throughput);
    refresh();

}

void sigterm_h(int signal){
   fprintf(stdout, "aplication recived signal%d \n", signal);
   clear();
   endwin();
   exit(0);
}

void sigalrm_h(int signal){
fprintf(stdout, "aplication recived signal%d \n", signal);
    clear();
    endwin();
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

    //структура для хранения времни блока измерения скорости
    struct timeval curr, prev;
    timeout_select.tv_sec = 1;
    timeout_select.tv_usec = 0;

    unsigned long long usec_diff;
    unsigned long long byte_cnt = 0;
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

    //print usage
    mvprintw(0,0,"Usage: press Ctrl + c to exit");

    // Setup UDP socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0){
        perror("Error creating temporary socket.\n");
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
    // настройка дискрипторов для неблокируемых сокетов
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

                //измеряем кол-во байт принятых за 1 сек
                gettimeofday(&curr, NULL);
                usec_diff = (curr.tv_sec * 1000000 + curr.tv_usec -
                prev.tv_sec * 1000000 - prev.tv_usec);
                byte_cnt += bytesize;
                if (curr.tv_sec > prev.tv_sec) {
                throughput = Mbps(byte_cnt, usec_diff);

                prev = curr;
                byte_cnt = 0;
                }
                //конец блока измерения

                memcpy(&datarx[i], bufrx, sizeof(udpdata_t));
                datarx[i].tscrx = rdtsc(); // ставим временную метку в пакет
                //timer_reset();

                //обнулим флаг конца буфера
                if (i > 1 && bufferisfull ) bufferisfull = 0;

                //Проверяем нумерацию пакетов.
                //если достигли конца массива, то сравниваем последний элемент с первым,
                //инчае по порядку
                int n = bufferisfull ? 1024 : i;

                //сброс счетчиков, когда принимаем 0 пакет, это происходит,  если перезагрузить клиента.
                if (datarx[i].seq == 0) {
                drop_pkt = 0;
                recv_pkt = 0;
                }
                //если принятых пакетов больше чем два, и нумерация принятых пакетов идет не по порядку
                if (recv_pkt > 2 && datarx[i].seq - datarx[n - 1].seq > 1) {
                    //увеличим счетчик дропов
                    int cnt_drop = (datarx[i].seq - datarx[n - 1].seq) - 1;
                    drop_pkt += cnt_drop;

                    //перезапрашиваем пакетики

                    for (;cnt_drop; cnt_drop--)
                    {
                    ((datatx_t *)buftx)->seq = datarx[n - 1].seq + cnt_drop; //заполняем структуру для udp пакета
                    sendto(sock,buftx,sizeof(datatx_t),0,&from,len);
                    mvprintw(5+cnt_drop,0,"DEBUG:  packet resend seq: %d",datarx[n - 1].seq + cnt_drop);
                    }
                }

                i++;
                //проверка на конец буфера
                if (i > npkts-1) {
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
     endwin();
    return(err);

err:
    err = 1;
    goto out;
}


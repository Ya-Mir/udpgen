/*
 *Клиент.
 *Посылает нумерованный поток udp пакетов на loopback интерфейс,
 *умеет пропускать пакеты по нажатию клавиши d
 * TODO bw не пропорционально влияет на кол-во трафика
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <curses.h>
#include "udptx.h"
#include <sys/fcntl.h>
#include <sys/select.h>


#define MAXPAYLOAD 1024
#define Mbps(bytes, usec) ((bytes) * 8. / (usec))


//#define SA      struct sockaddr
__inline__ unsigned long long rdtsc(void)
{
    unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    return ( (unsigned long long)lo)|( ((unsigned long long)hi)<<32 );
}

typedef struct {
    uint32_t seq;
    uint64_t tsctx;
    uint64_t tscrx;
    char payload[PAYLOADSIZE];
    } datatx;



udpretransmission_t *udpdata;
int rate = 2; //Mbps
int       npkts = 1024; //размер буфера под пакеты
char payloadPkt[] = "1";
    int max = 0;
    int successPkg = 0;
    int totalPkg = 0;
    int pktdrop = 0;
    int numfailchecksum = 0;
    int plen = 128;
    double bw = 20000000.0; //byte*s
    void *buf  = NULL;
    void *bufx  = NULL;



char keyscan(){
	int ch;
        ch = getch();
    //logic for key d (drop)
    if (ch == 100 || ch == 68 ) {
    pktdrop++;
	}
    if ( ch > 48 && ch < 58){
    rate = ch - '0';
    }

	return ch;
}

int main(void)
{
    //------ncurses init----------------------
    WINDOW *w = initscr();
    noecho();
    cbreak();
    nodelay(w, TRUE);
    if(has_colors() == FALSE)
    {	endwin();
            printf("Your terminal does not support color\n");
            exit(1);
    }
    start_color();
    // -------------end ncurses---------------


    double throughput = 0;
    int sock;
    uint64_t num = 0; //нумерация посланных пакетов
    int numrx = 1;
    ssize_t bytes_sent;
    unsigned long long usec_diff;
    unsigned long long byte_cnt = 0;

    struct sockaddr_in addr;
    struct sockaddr_in client_addr;
    struct timeval tim;
    struct timeval timeout_select;
    struct sockaddr from;
    unsigned int len = sizeof(from);
    void               *bufrx  = NULL;

    int err;
    unsigned long long rdtsc();
    size_t bufsz = sizeof(udpdata_t);
    size_t bufsztx = sizeof(datatx);
    int bufszrx = sizeof(udpretransmission_t);
    int bytesize = 0;
    udpdata_t *udpdatatx;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(3425); //none priv port
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(506); //priv port, use root
    client_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    struct timeval read_timeout;
    struct timeval curr, prev;
    read_timeout.tv_sec = 0;
    read_timeout.tv_usec = 1000;
    timeout_select.tv_sec = 1;
    timeout_select.tv_usec = 0;
    //print usage
    mvprintw(0,0,"Usage: press key 'd' for drop packet. 1,2,3.. - change speed");
    //create socket, return fd num
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock < 0)
    {
        perror("can not create socket");
        return 1;
    }

    //abstract connect, for use send
   if (bind(sock,(struct sockaddr *)&client_addr,sizeof(struct sockaddr)) < 0)
     {
         perror("failed bind");
     }


   if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("failed connect to server");
        return 2;
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

    // Allocate receive data buffer
    if ((udpdata= (udpretransmission_t *)calloc(npkts, sizeof(*udpdata))) == NULL)

    // Allocate sending buffer
      if ((buf = malloc(bufsz)) == NULL){
        perror("malloc tx");
        return 11;
    }
    bufx = malloc(sizeof(datatx));

    // Allocate rx buffer
   if ((bufrx = malloc(bufszrx)) == NULL){
   perror("malloc rx");
        return 12;
   }

    //цикл для приема и отправки пакетов
    for (;;) {
        FD_ZERO(&readfds);
        FD_SET(sock, &readfds);
        if (select(sock+1, &readfds, &writefds, NULL,&timeout_select) == -1) {
            perror("select");
            exit(4);
        }


        if (FD_ISSET(sock, &readfds)) {
            //-------------прием--------
            bytesize = recvfrom(sock, bufrx, bufszrx, 0,&from, &len);
            if (bytesize == 0){
                mvprintw (5,0, "ERROR: Server %s, please check it",strerror(errno));
            } else
            if (bytesize < 0){
                mvprintw (5,0, "ERROR: Server %s, please check it",strerror(errno));
                refresh();
                //goto err;
            } else
            if (bytesize != bufszrx){
                 mvprintw (5,0, "WARNING: Server %s, please check it",strerror(errno));
                //goto err;
            } else
            if (bytesize == bufszrx) {
            //парсим какой пакет, нужно перепослать
                memcpy(&udpdata[numrx], bufrx, sizeof(udpretransmission_t));
                mvprintw (6,0, "DEBUG: Server request resend packet %d",udpdata[numrx].seq);
                numrx++;
                if (numrx > 1024) numrx=1;
              }          
          }  //-------конец приема

        //блок для ограничения скорости передачи пакетов
        gettimeofday(&curr, NULL);
        usec_diff = (curr.tv_sec * 1000000 + curr.tv_usec -
        prev.tv_sec * 1000000 - prev.tv_usec);
        if (usec_diff == 0)
        continue;
        if (Mbps(byte_cnt, usec_diff) >= rate)
        continue;

        if (FD_ISSET(sock, &writefds)) {



            gettimeofday(&tim, NULL);
            //заполняем структуру для udp пакета
            ((datatx*)bufx)->seq = num;
            ((datatx *)bufx)->tsctx = rdtsc();
            strcpy(((datatx *)bufx)->payload,"simpled payload line"); //для упрощения передаем в payload константную строку

            if (keyscan() == 'd') {
                num++;
                continue; //при нажатии клавиши пропускаем отсылку, имитируем потерю пакета
            }

            bytes_sent = send(sock, bufx, bufsztx, 0);
           //проверка на успешность отосылки пакета
            if (bytes_sent < 0) {
                perror("Error sending packet.\n");
                close(sock);
                return 5;
            } else {
                byte_cnt += bytes_sent;

                if (curr.tv_sec > prev.tv_sec) {
                throughput = Mbps(byte_cnt, usec_diff);
                mvprintw(2,0,"BANDWIDTH: %.2f Mbit/s ",throughput);
                prev = curr;
                byte_cnt = 0;
                }

                num++;
                mvprintw(1,0,"SENDING PACKET SEQ:%d", num);
                mvprintw(3,0,"PACKET DROP: %d", pktdrop);
                refresh();
        }
   }
}
    return 0;
}


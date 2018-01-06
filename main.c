/*
 *Клиент.
 *Шлет нумерованный поток udp пакетов на loopback интерфейс,
 *умеет пропускать пакеты по нажатию клавиши d
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



#define MAXPAYLOAD 1024

typedef struct {
    uint32_t seq;
    uint64_t tsctx;
    uint64_t tscrx;
    char payload[PAYLOADSIZE];
    } datatx;



udpretransmission_t *udpdata;
int       npkts = 1024; //размер буфера под пакеты
char payloadPkt[] = "1";
//char buf[1024];
    int max = 0;
    int successPkg = 0;
    int totalPkg = 0;
    int pktdrop = 0;
    int numfailchecksum = 0;
    int plen = 128;
    double bw = 30000.0;
    void *buf  = NULL;
    void *bufx  = NULL;
    int keyscan(){
	int ch;
        ch = getch();
	//key d (drop)
	if (ch == 100) {
	pktdrop++;
	}
	return ch;

}

int main(void)
{
//    ncurses init
    WINDOW *w = initscr();
    noecho();
    cbreak();
    nodelay(w, TRUE);
   // cbreak();
  //декларируем и инициализируем переменные
    int sock;
    uint32_t num = 1; //нумерация пакетов
    ssize_t bytes_sent;
    struct sockaddr_in addr;
    struct timeval tim;
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
    //udpretransmission_t *udpdatarx;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(3425); //none priv port
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    struct timeval read_timeout;
    read_timeout.tv_sec = 0;
    read_timeout.tv_usec = 10;

    //create socket, return fd num
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if(sock < 0)
    {
        perror("can not create socket");
        return 1;
    }
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof read_timeout);
    //abstract connect, for use send
    if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("failed connect to server");
        return 2;
    }

    // Allocate receive data buffer
    if ((udpdata= (udpretransmission_t *)calloc(npkts, sizeof(*udpdata))) == NULL)

    // Allocate sending buffer
      if ((buf = malloc(bufsz)) == NULL){
        perror("malloc tx");
        return 11;
   }
    bufx = malloc(sizeof(datatx));
    printw("sdfsdfsdf");
   // Allocate rx buffer
   if ((bufrx = malloc(bufszrx)) == NULL){
   perror("malloc rx");
        return 12;
   }

    //цикл для отправки пакетов
    for (;;) {
        gettimeofday(&tim, NULL);
        double time_stamp=tim.tv_sec+(tim.tv_usec/1000000.0);
        //заполняем структуру для udp пакета
        ((datatx*)bufx)->seq = num;
        //((udpdata_t *)buftx)->seq = num;
        ((datatx *)bufx)->tsctx = time_stamp;
        strcpy(((datatx *)bufx)->payload,"simpled payload line"); //для упрощения передаем в payload константную строку
        if (keyscan() == 100) goto drop; //при нажатии клавиши пропускаем отсылку, имитируем потерю пакета
        bytes_sent = send(sock, bufx, bufsztx, 0);
drop:
	//послушаем, не хотят ли нам чего сказать
	bytesize = recvfrom(sock, bufrx, bufszrx, 0,&from, &len);
	if (bytesize == 0){
	    fprintf(stdout, "NOOP.\n");
	} else
	if (bytesize < 0){
	    perror("recvfrom");

	    //goto err;
	} else
	if (bytesize != bufszrx){
	    fprintf(stdout, "Received unknown packet.\n");
	    //goto err;
	} else
	    if (bytesize == bufszrx) {
		fprintf(stdout, "Received packet.\n");
	//парсим какой пакет, нужно перепослать
      // memcpy(&udpdatarx[num], bufrx, sizeof(udpretransmission_t));
      //  printf ("retransmit packet seq %d", udpdatarx[num].seq);
        }

        //задержка, формирует скорость потока
        int interval = (int) (1000000.0 * plen / bw); usleep(interval);
        if (bytes_sent < 0) {
          perror("Error sending packet.\n");
          close(sock);
          return 5;

        } else {
        num++;
        printf("SENDING PACKET SEQ#[%d] LENGTH [%d] BYTES BANDWIDTH [%f] BYTES/SEC LENGTH [%d] PACKET DROP [%d]\r",
        num, plen, bw,bytes_sent,pktdrop);
        refresh();
  }

}

    return 0;

}

/*void req_retrans(int num){
  ((udpdata_t *)buf)->seq = num;
  //посылаем перезапрашивыемый пакет
  bytes_sent = send(sock, buf, bufsz, 0);
}
*/
void pint_drop(){
  int drop = max - successPkg - numfailchecksum;
  printf("PERCENT DROP: %d/%d = %.2f %%\n", drop, max, 100.0 * drop / max);
}

void dropPacket(){

}


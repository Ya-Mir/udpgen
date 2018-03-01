/*
 *Клиент.
 *Посылает нумерованный поток udp пакетов на loopback интерфейс,
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
#include <sys/fcntl.h>
#include <sys/select.h>


#define MAXPAYLOAD 1024
#define SA      struct sockaddr

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

int connect_nonb(int, const SA *saptr, socklen_t salen, int nsec);

char keyscan(){
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



    int sock;
    uint32_t num = 1; //нумерация посланных пакетов
    int numrx = 1;
    ssize_t bytes_sent;
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
    //udpretransmission_t *udpdatarx;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(3425); //none priv port
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(506); //priv port, use root
    client_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    struct timeval read_timeout;
    read_timeout.tv_sec = 0;
    read_timeout.tv_usec = 1000;
    timeout_select.tv_sec = 1;
    timeout_select.tv_usec = 0;
    //usage
    mvprintw(0,0,"Usage: press key 'd' for drop packet");
    //create socket, return fd num
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock < 0)
    {
        perror("can not create socket");
        return 1;
    }
//    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &read_timeout, sizeof (read_timeout))<0)
//    {
//        perror("failed set socket option");
//    }

    //abstract connect, for use send
   if (bind(sock,(struct sockaddr *)&client_addr,sizeof(struct sockaddr)) < 0)
     {
         perror("failed bind");
     }
   else {
     printw("bind succe\n");
      }

   if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
   //if(connect_nonb(sock, (struct sockaddr *)&addr, sizeof(addr),0) < 0)
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
                //TODO num - здесь не должно быть, завсти переменную
                memcpy(&udpdata[numrx], bufrx, sizeof(udpretransmission_t));
                mvprintw (6,0, "DEBUG: Server request resend packet %d",udpdata[numrx].seq);
                numrx++;
                if (numrx > 1024) numrx=1;
              }
            //-------конец приема

          }

        if (FD_ISSET(sock, &writefds)) {

            gettimeofday(&tim, NULL);
            double time_stamp=tim.tv_sec+(tim.tv_usec/1000000.0);
            //заполняем структуру для udp пакета
            ((datatx*)bufx)->seq = num;
            //((udpdata_t *)buftx)->seq = num;
            ((datatx *)bufx)->tsctx = time_stamp;
            strcpy(((datatx *)bufx)->payload,"simpled payload line"); //для упрощения передаем в payload константную строку


            if (keyscan() == 'd') goto drop; //при нажатии клавиши пропускаем отсылку, имитируем потерю пакета
            bytes_sent = send(sock, bufx, bufsztx, 0);
drop:
            //послушаем, не хотят ли нам чего сказать
            if (bytes_sent < 0) {
                perror("Error sending packet.\n");
                close(sock);
                return 5;


            } else {
                num++;
                mvprintw(1,0,"SENDING PACKET SEQ:%d", num);
                mvprintw(2,0,"BANDWIDTH: %f BYTES/SEC ",bw);
                mvprintw(3,0,"PACKET DROP: %d", pktdrop);
                refresh();
        }






        //задержка, формирует скорость потока

    }

        int interval = (int) (1000000.0 *   plen / bw); usleep(interval);
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


//int
//connect_nonb(int sockfd, const SA *saptr, socklen_t salen, int nsec)
//{
//	int				flags, n, error;
//	socklen_t		len;
//	fd_set			rset, wset;
//	struct timeval	tval;

//    flags = fcntl(sockfd, F_GETFL, 0);
//    fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);

//	error = 0;
//	if ( (n = connect(sockfd, saptr, salen)) < 0)
//		if (errno != EINPROGRESS)
//			return(-1);

//	/* Do whatever we want while the connect is taking place. */

//	if (n == 0)
//		goto done;	/* connect completed immediately */

//	FD_ZERO(&rset);
//	FD_SET(sockfd, &rset);
//	wset = rset;
//	tval.tv_sec = nsec;
//	tval.tv_usec = 0;

//    if ( (n = select(sockfd+1, &rset, &wset, NULL,
//					 nsec ? &tval : NULL)) == 0) {
//		close(sockfd);		/* timeout */
//		errno = ETIMEDOUT;
//		return(-1);
//	}

//	if (FD_ISSET(sockfd, &rset) || FD_ISSET(sockfd, &wset)) {
//		len = sizeof(error);
//		if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len) < 0)
//			return(-1);			/* Solaris pending error */
//	} else
//	printf("select error: sockfd not set");
//        return (1);


//done:
//    fcntl(sockfd, F_SETFL, flags);	/* restore file status flags */

//	if (error) {
//		close(sockfd);		/* just in case */
//		errno = error;
//		return(-1);
//	}
//	return(0);
//}

//int
//writable_timeo(int fd, int sec)
//{
//        fd_set                  wset;
//        struct timeval  tv;

//        FD_ZERO(&wset);
//        FD_SET(fd, &wset);

//        tv.tv_sec = sec;
//        tv.tv_usec = 0;

//        return(select(fd+1, NULL, &wset, NULL, &tv));
//                /* > 0 if descriptor is writable */
//}
///* end writable_timeo */

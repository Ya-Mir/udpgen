/*
 * Клиент.
 * Посылает нумерованный поток udp пакетов на loopback интерфейс,
 * умеет пропускать пакеты по нажатию клавиш.
 * d - 1 пакет, f - 4 пакета
 *
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



#define Mbps(bytes, usec) ((bytes) * 8. / (usec))



typedef struct {
    uint32_t seq;
    uint64_t tsctx;
    uint64_t tscrx;
    char payload[PAYLOADSIZE];
    } datatx;



udpretransmission_t *udpdata; //структура для пакета перазапроса
int rate = 2; //Mbps - Скорость потока по-умолчанию
int       npkts = 1024; //размер буфера под пакеты
int pktdrop = 0; // кол-во потеряных пакетов
    void *bufrx  = NULL; //буфер для приема
    void *buftx  = NULL; //буфер для передачи
int enable_debug = 0;

//обработчик нажатия клавиш
char keyscan(){
	int ch;
        ch = getch();
    //клавиша d(D) - потеря пакета
    if (ch == 100 || ch == 68 ) {
    pktdrop++;
	}

    if (ch == 102 ) {
    pktdrop += 4;
    }
    // клавиши 1 .. 9 - смена скорости
    if ( ch > 48 && ch < 58){
    rate = ch - '0';
    }
    //клавиша e - дебаг
    if (ch == 101 ||ch == 69 ) {

        if(enable_debug){
                mvprintw(4,0,"'e' - enable debug");
                enable_debug=!enable_debug;
                move (10,0);
                clrtoeol();
        } else {
            move (4,0);
            clrtoeol();
            move (10,0);
            clrtoeol();
            mvprintw(4,0,"'e' - disable debug");
            enable_debug=!enable_debug;
        }
    }

	return ch;
}

int main(void)
{
    //инициализация библиотеки ncurses
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
    init_pair (1, COLOR_RED, COLOR_BLACK);
    init_pair (2, COLOR_WHITE, COLOR_BLACK);
    //конец ncurses
    double throughput = -1; //скорость генерации пакетов
    int sock;
    int numrx = 1;
    uint32_t num = 0; //нумерация посланных пакетов
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
    size_t bufsz = sizeof(udpdata_t);
    size_t bufsztx = sizeof(datatx);
    int bufszrx = sizeof(udpretransmission_t);
    int bytesize = 0;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UDPPORT);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(506); //priv port, use root
    client_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    //структура для хранения времни блока ограничения скорости
    struct timeval curr, prev;
    timeout_select.tv_sec = 1;
    timeout_select.tv_usec = 0;

    //print usage
    mvprintw(0,0,"Usage: press key");
    mvprintw(1,0,"'d' for 1 drop packet.");
    mvprintw(2,0,"'f' for 4 drop packets.");
    mvprintw(3,0,"1,2,3... - change speed");
    mvprintw(4,0,"'e' - enable/disable debug");

    //откроем socket
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if(sock < 0)
    {
        perror("can not create socket");
        return 1;
    }


   if (bind(sock,(struct sockaddr *)&client_addr,sizeof(struct sockaddr)) < 0)
     {
         perror("failed bind");
     }

    //abstract connect, for use send
   if(connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        perror("failed connect to server");
        return 2;
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

    // выделим буфер под хранение пакетов перезапроса
    if ((udpdata= (udpretransmission_t *)calloc(npkts, sizeof(*udpdata))) == NULL)

    // выделим tx буфер
      if ((bufrx = malloc(bufsz)) == NULL){
        perror("malloc tx");
        return 11;
    }
    buftx = malloc(sizeof(datatx));

    // выделим rx буфер
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
                //attron (COLOR_PAIR(1));
                mvprintw (9,0, "ERROR: Server %s, please check it",strerror(errno));
            } else
            if (bytesize < 0){
                attron (COLOR_PAIR(1));
                mvprintw (9,0, "ERROR: Server %s, please check it",strerror(errno));
                refresh();
                attroff (COLOR_PAIR(1));

                //goto err;
            } else
            if (bytesize != bufszrx){
                 mvprintw (9,0, "WARNING: Server %s, please check it",strerror(errno));
                //goto err;
            } else
            if (bytesize == bufszrx) {
                mvprintw (9,0, "");
                //парсим какой пакет, нужно перепослать
                memcpy(&udpdata[numrx], bufrx, sizeof(udpretransmission_t));
                enable_debug ? mvprintw (10,0, "DEBUG: Server request resend packet %d",udpdata[numrx].seq):0;
                numrx++;
                if (numrx > npkts) numrx=1;
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
            //при нажатии клавиши d пропускаем отсылку, имитируем потерю пакета
            char key = keyscan();
            if ( key == 'd') {
                num++;
             //   continue;
            }
            else if (key == 'f') {
                num += 4;
            }
            ((datatx*)buftx)->seq = num;
            ((datatx *)buftx)->tsctx = rdtsc();
            //для упрощения передаем в payload константную строку
            strcpy(((datatx *)buftx)->payload,"simpled payload line");


            bytes_sent = send(sock, buftx, bufsztx, 0);
           //проверка на успешность отсылки пакета
            if (bytes_sent < 0) {
                perror("Error sending packet.\n");
                close(sock);
                return 5;
            } else {
                byte_cnt += bytes_sent;
                //считаем скорость отправки пакетов
                if (curr.tv_sec > prev.tv_sec) {
                throughput = Mbps(byte_cnt, usec_diff);
                mvprintw(5,0,"BANDWIDTH: %.2f Mbit/s ",throughput);
                prev = curr;
                byte_cnt = 0;
                }

                num++;
                mvprintw(6,0,"SENDING PACKET SEQ:%d", num);
                mvprintw(7,0,"PACKET DROP: %d", pktdrop);
                refresh();
        }
   }
}
    return 0;
}


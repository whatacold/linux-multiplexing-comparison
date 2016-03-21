#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <time.h> 
#include <signal.h> 

int
main(int argc, char *argv[])
{
    int listenfd = 0, connfd = 0, rc = -1;
    struct sockaddr_in serv_addr; 

    char sendBuff[1025];
    time_t ticks; 
    struct timespec ts;

    sigset_t sigset;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, 0, sizeof(serv_addr));
    memset(sendBuff, 0, sizeof(sendBuff)); 

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(12346); 

    bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)); 

    listen(listenfd, 10); 

    /*
     * block SIGPIPE, which will be raised by write(2) below
     * when peer closing the pipe
     */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGPIPE);
    sigprocmask(SIG_BLOCK, &sigset, NULL);

    while(1) {
        connfd = accept(listenfd, (struct sockaddr *)NULL, NULL); 

        do {
            ticks = time(NULL);
            snprintf(sendBuff, sizeof(sendBuff), "%.24s\n", ctime(&ticks));
            rc = write(connfd, sendBuff, strlen(sendBuff)); 
            ts.tv_sec = 0;
            ts.tv_nsec = 1000;
            nanosleep(&ts, NULL);
        } while(rc != -1);

        close(connfd);
    }

    return 0;
}

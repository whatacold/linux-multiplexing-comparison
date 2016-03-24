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
    int listenfd = 0, connfd = 0, reuse;
    struct sockaddr_in serv_addr; 

    sigset_t sigset;

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(12345); 

    bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)); 

    listen(listenfd, 10); 

    /*
     * block SIGPIPE, which will be raised by write(2) below
     * when peer closing the pipe
     */
    sigemptyset(&sigset);
    sigaddset(&sigset, SIGPIPE);
    sigprocmask(SIG_BLOCK, &sigset, NULL);

    /* TODO handle peer's closing */
    while(1) {
        /**
         * conn is accepted, but never get handled.
         * XXX and this fd will get lost, and get closed only after exit.
         */
        connfd = accept(listenfd, (struct sockaddr *)NULL, NULL); 
    }

    return 0;
}

/**
 * 意图：想测试select poll epoll三种方式在fd很多的情况下的性能差异。
 * 但是种种原因导致没有测试成功。
 *
 * 1. select支持的句柄数，在系统宏FD_SETSIZE中限制死了，只有1024个，太小了导致select和poll没看到有明显的差别。
 * 2. epoll不支持添加常规文件（如/dev/null，但是终端STDIN_FILENO是可以的）的句柄，又不能添加重复句柄，导致epoll都没有测成功。
 *
 * 下一步计划：
 * nc监听一个端口，同时通过重定向发送小文件给对方（制造读事件）；
 * 此处建立到nc的连接。
 * 想办法打破 FD_SETSIZE 的限制，扩大fd数量。
 *
 *
 * http://stackoverflow.com/questions/7976388/increasing-limit-of-fd-setsize-and-select
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/poll.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>

#define READ_SIZE 10
#define TIMEOUT_SEC 60

int *fds;
fd_set *fd_set_array;
struct pollfd *pfds;
struct epoll_event *evs;
int nceil;
int ncur;
char buf[READ_SIZE + 1] = {0};

unsigned long result[4][1000]; // FIXME alloc dynamically

unsigned long
do_select()
{
    int max = -1, i, rc;
    struct timeval ts;
    struct timeval start, end;

    FD_ZERO(fd_set_array);
    for(i = 0; i < ncur - 1; i++) {
        FD_SET((fds[i] % FD_SETSIZE), (fd_set_array + (fds[i] / FD_SETSIZE)));
    }
    FD_SET((fds[nceil - 1] % FD_SETSIZE), (fd_set_array + (fds[nceil - 1] / FD_SETSIZE)));
    max = fds[nceil - 1] + 1;

    ts.tv_sec = TIMEOUT_SEC;
    ts.tv_usec = 0;
    gettimeofday(&start, NULL);
    rc = select(max, fd_set_array, NULL, NULL, &ts);
    gettimeofday(&end, NULL);
    switch(rc) {
    case -1:
        fprintf(stderr, "select failed: %s\n", strerror(errno));
        break;
    case 0:
        fprintf(stderr, "select timed out\n");
        break;
    default:
        fprintf(stderr, "%d fds are ready for reading.\n", rc);
        for(i = 0; i < ncur - 1; i++) {
            if(FD_ISSET((fds[i] % FD_SETSIZE), &fd_set_array[fds[i] / FD_SETSIZE])) {
                read(fds[i], buf, READ_SIZE);
            }
        }
        if(FD_ISSET((fds[nceil - 1] % FD_SETSIZE), &fd_set_array[fds[nceil - 1] / FD_SETSIZE])) {
            read(fds[nceil - 1], buf, READ_SIZE);
        }
        break;
    }

    return (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
}

unsigned long
do_poll()
{
    int i, rc;
    struct timeval start, end;

    for(i = 0; i < ncur - 1; i++) {
        memset(&pfds[i], 0, sizeof(pfds[i]));
        pfds[i].fd = fds[i];
        pfds[i].events = POLLIN;
    }
    memset(&pfds[i], 0, sizeof(pfds[i]));
    pfds[i].fd = fds[nceil - 1];
    pfds[i].events = POLLIN;

    gettimeofday(&start, NULL);
    rc = poll(pfds, ncur, TIMEOUT_SEC * 1000);
    gettimeofday(&end, NULL);
    switch(rc) {
    case -1:
        fprintf(stderr, "poll failed: %s\n", strerror(errno));
        break;
    case 0:
        fprintf(stderr, "poll timed out\n");
        break;
    default:
        fprintf(stderr, "%d fds are ready for reading.\n", rc);
        for(i = 0; i < ncur - 1; i++) {
            if(pfds[i].revents & POLLIN) {
                read(pfds[i].fd, buf, READ_SIZE);
            }
        }
        break;
    }

    return (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
}

unsigned long
do_epoll()
{
    int i, rc, epfd, flags;
    struct epoll_event ev;
    struct timeval start, end;

    epfd = epoll_create1(0);
    assert(epfd > 0);

    /*
     * XXX
     * epoll don't support regular files or directory,
     * and disallows adding duplicate fd.
     */
    for(i = 0; i < ncur - 1; i++) {
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLET;
        ev.data.fd = fds[i];
        rc = epoll_ctl(epfd, EPOLL_CTL_ADD, fds[i], &ev);
        assert(0 == rc);
    }
    flags = fcntl(fds[nceil - 1], F_GETFL);
    flags |= O_NONBLOCK;
    assert(0 == fcntl(fds[nceil - 1], F_SETFL, flags));
    memset(&ev, 0, sizeof(ev));
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = fds[nceil - 1];
    rc = epoll_ctl(epfd, EPOLL_CTL_ADD, fds[nceil - 1], &ev);
    assert(0 == rc);

    gettimeofday(&start, NULL);
    rc = epoll_wait(epfd, evs, ncur, TIMEOUT_SEC * 1000);
    gettimeofday(&end, NULL);
    switch(rc) {
    case -1:
        fprintf(stderr, "epoll failed: %s\n", strerror(errno));
        break;
    case 0:
        fprintf(stderr, "epoll timed out\n");
        break;
    default:
        fprintf(stderr, "%d fds are ready for reading.\n", rc);
        for(i = 0; i < rc; i++) {
            read(evs[i].data.fd, buf, READ_SIZE);
        }
        break;
    }

    return (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
}

int
is_ready()
{
    struct timeval tv;
    fd_set rset;

    FD_ZERO(&rset);
    FD_SET(fds[nceil - 1], &rset);
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    return (1 == select(fds[nceil - 1] + 1, &rset, NULL, NULL, &tv));
}

int
main(int argc, char **argv)
{
    int i, pass, init, step;
    unsigned long elapsed = 0;
    struct sockaddr_in saddr;

    if(5 != argc) {
        fprintf(stderr, "Usage: %s <pass> <nceil> <init> <step>\n", argv[0]);
        exit(1);
    }
    pass = atoi(argv[1]);
    nceil = atoi(argv[2]);
    init = atoi(argv[3]);
    step = atoi(argv[4]);

    /* alloc memory */
    fds = calloc(nceil, sizeof(int));
    fd_set_array = calloc((nceil / FD_SETSIZE) + 1, sizeof(fd_set)); /* maybe one fd_set more than expected */
    pfds = calloc(nceil, sizeof(struct pollfd));
    evs = calloc(nceil, sizeof(struct epoll_event));
    assert(fds && fd_set_array && pfds && evs);

    /* (nceil-1) sockets which don't receive data */
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(12345);
    inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
    for(i = 0; i < nceil - 1; i++) {
        fprintf(stderr, "build connection #%d\n", i);
        fds[i] = socket(AF_INET, SOCK_STREAM, 0);
        assert(fds[i] >= 0);
        assert(0 == connect(fds[i], (struct sockaddr *)&saddr, sizeof(saddr)));
    }

    /* the only socket that has data to receive */
    saddr.sin_port = htons(12346);
    fds[i] = socket(AF_INET, SOCK_STREAM, 0);
    assert(fds[i] >= 0);
    assert(0 == connect(fds[i], (struct sockaddr *)&saddr, sizeof(saddr)));

    int round = 0;
    for(ncur = init; ncur <= nceil; ncur += step) {
        fprintf(stderr, "round with number of fds: %d\n", ncur);

        result[0][round] = ncur;

        elapsed = 0;
        for(i = 0; i < pass; i++) {
            while(!is_ready());
            elapsed += do_select();
        }
        result[1][round] = elapsed / pass;
        fprintf(stderr, "elapsed time of select(): %dus\n", elapsed / pass);

        elapsed = 0;
        for(i = 0; i < pass; i++) {
            while(!is_ready());
            elapsed += do_poll();
        }
        result[2][round] = elapsed / pass;
        fprintf(stderr, "elapsed time of poll(): %dus\n", elapsed / pass);

        elapsed = 0;
        for(i = 0; i < pass; i++) {
            while(!is_ready());
            elapsed += do_epoll();
        }
        result[3][round] = elapsed / pass;
        fprintf(stderr, "elapsed time of epoll(): %dus\n", elapsed / pass);

        round++;
    }

    printf("# #fds\tselect\tpoll\tepoll\n", result[0][i], result[1][i], result[2][i], result[3][i]);
    for(i = 0; i < round; i++) {
        printf("%lu\t%lu\t%lu\t%lu\n", result[0][i], result[1][i], result[2][i], result[3][i]);
    }
}

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

#define FD_NUM (FD_SETSIZE * 5 + 100)
#define READ_SIZE 10
#define TIMEOUT_SEC 5

#if 0
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#error "FD_NUM exceeds FD_SETSIZE"
#define STR_MSG "foo"
#pragma message "a compilation msg " STR_MSG
#pragma message "FD_SETSIZE = " #FD_SETSIZE
#endif

int fds[FD_NUM] = {0};
char buf[READ_SIZE + 1] = {0};
fd_set fd_set_array[FD_NUM / FD_SETSIZE + 1]; /* 当FD_NUM刚好是FD_SETSIZE整数倍时，会多一份 */

unsigned long
do_select()
{
    int max = -1, i, rc;
    struct timeval ts;
    struct timeval start, end;

    FD_ZERO(fd_set_array);
    for(i = 0; i < FD_NUM; i++) {
        FD_SET((fds[i] % FD_SETSIZE), (&fd_set_array[fds[i] / FD_SETSIZE])); /* XXX FD_NUM需要是FD_SETSIZE的整数倍 */
    }
    max = fds[i - 1] + 1;

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
        for(i = 0; i < FD_NUM; i++) {
            if(FD_ISSET((fds[i] % FD_SETSIZE), &fd_set_array[fds[i] / FD_SETSIZE])) {
                read(fds[i], buf, READ_SIZE);
            }
        }
        break;
    }

    return (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
}

unsigned long
do_poll()
{
    int i, rc;
    struct pollfd pfds[FD_NUM];
    struct timeval start, end;

    for(i = 0; i < FD_NUM; i++) {
        memset(&pfds[i], 0, sizeof(pfds[i]));
        pfds[i].fd = fds[i];
        pfds[i].events = POLLIN;
    }
    gettimeofday(&start, NULL);
    rc = poll(pfds, FD_NUM, TIMEOUT_SEC * 1000);
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
        for(i = 0; i < FD_NUM; i++) {
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
    int i, rc, epfd;
    struct epoll_event ev, evs[FD_NUM];
    struct timeval start, end;

    epfd = epoll_create1(0);
    assert(epfd > 0);
    for(i = 0; i < FD_NUM; i++) {
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN | EPOLLET;
        fds[i] = STDIN_FILENO;
        ev.data.fd = fds[i];    // XXX epoll don't support regular files or directory, and disallows adding duplicate fd.
        rc = epoll_ctl(epfd, EPOLL_CTL_ADD, fds[i], &ev);
        assert(0 == rc);
    }
    gettimeofday(&start, NULL);
    rc = epoll_wait(epfd, evs, FD_NUM, TIMEOUT_SEC * 1000);
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
main(int argc, char **argv)
{
    int i, cnt;
    unsigned long elapsed = 0;
    struct sockaddr_in saddr;

    if(2 != argc) {
        fprintf(stderr, "Usage: %s num\n", argv[0]);
        exit(1);
    }
    cnt = atoi(argv[1]);

    fprintf(stderr, "FD_NUM=%d\n", FD_NUM);

    /* FD_NUM个仅连接，但是没有数据可接收的socket */
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(12345);
    inet_pton(AF_INET, "127.0.0.1", &saddr.sin_addr);
    for(i = 0; i < FD_NUM - 1; i++) {
        fprintf(stderr, "build connection #%d\n", i);
        fds[i] = socket(AF_INET, SOCK_STREAM, 0);
        assert(fds[i] >= 0);
        assert(0 == connect(fds[i], (struct sockaddr *)&saddr, sizeof(saddr)));
    }

    /* 留一个连接有数据可接收的socket */
    saddr.sin_port = htons(12346);
    fds[i] = socket(AF_INET, SOCK_STREAM, 0);
    assert(fds[i] >= 0);
    assert(0 == connect(fds[i], (struct sockaddr *)&saddr, sizeof(saddr)));

    elapsed = 0;
    for(i = 0; i < cnt; i++) {
        elapsed += do_select();
    }
    fprintf(stderr, "elapsed time of select(): %d\n", elapsed);
    return 0;

    elapsed = 0;
    for(i = 0; i < cnt; i++) {
        elapsed += do_poll();
    }
    fprintf(stderr, "elapsed time of poll(): %d\n", elapsed);

    elapsed = 0;
    for(i = 0; i < cnt; i++) {
        elapsed += do_epoll();
    }
    fprintf(stderr, "elapsed time of epoll(): %d\n", elapsed / 1000);
}

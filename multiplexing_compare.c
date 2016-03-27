/**
 * Demotrate the performance differences among the three I/O multiplexing mechanisms on Linux,
 * i.e. select, poll and epoll.
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
#define TIMEOUT_SEC 60      /* TODO make it self-adaptive */

int *fds;
unsigned char *rdset;
int rdset_nbytes;
struct pollfd *pfds;
struct epoll_event *evs;
int nceil;
int ncur;
char buf[READ_SIZE + 1] = {0};

unsigned long result[4][1000]; /* TODO alloc dynamically */

/**
 * below functions are similar to `FD_*' macros of select,
 * based on the assumption that the set is a byte array.
 */
void
MY_FD_SET(int fd, unsigned char *set)
{
    int slot;

    slot = fd / 8;
    set[slot] |= 1L << (fd % 8);
}

int
MY_FD_ISSET(int fd, const unsigned char *set)
{
    int slot;

    slot = fd / 8;
    return set[slot] & (1L << (fd % 8));
}

void
MY_FD_ZERO(unsigned char *set, int nbytes)
{
    memset(set, 0, nbytes);
}

int
MY_FD_SET_NBYTES(int max_fd)
{
    return max_fd / 8 + 1;
}

unsigned long
do_select()
{
    int max = -1, i, rc;
    struct timeval ts;
    struct timeval start, end;
    int fd;

    MY_FD_ZERO(rdset, rdset_nbytes);
    for(i = 0; i < ncur - 1; i++) {
        fd = fds[i];
        MY_FD_SET(fd, rdset);
    }
    fd = fds[nceil - 1];
    MY_FD_SET(fd, rdset);
    max = fds[nceil - 1] + 1;

    ts.tv_sec = TIMEOUT_SEC;
    ts.tv_usec = 0;
    gettimeofday(&start, NULL);
    rc = select(max, (fd_set *)rdset, NULL, NULL, &ts);
    gettimeofday(&end, NULL);
    switch(rc) {
    case -1:
        fprintf(stderr, "select failed: %s\n", strerror(errno));
        break;
    case 0:
        fprintf(stderr, "select timed out\n");
        break;
    default:
        //fprintf(stderr, "%d fds are ready for reading.\n", rc);
        for(i = 0; i < ncur - 1; i++) {
            fd = fds[i];
            if(MY_FD_ISSET(fd, rdset)) {
                read(fds[i], buf, READ_SIZE);
            }
        }
        fd = fds[nceil - 1];
        if(MY_FD_ISSET(fd, rdset)) {
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
        //fprintf(stderr, "%d fds are ready for reading.\n", rc);
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
     * epoll doesn't support regular files or directory,
     * and refuses adding duplicate fd.
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
        //fprintf(stderr, "%d fds are ready for reading.\n", rc);
        for(i = 0; i < rc; i++) {
            while(-1 != read(evs[i].data.fd, buf, READ_SIZE));
        }
        break;
    }
    close(epfd);

    return (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_usec - start.tv_usec);
}

int
is_ready()
{
    struct timeval tv;
    int fd;

    MY_FD_ZERO(rdset, rdset_nbytes);
    fd = fds[nceil -1];
    MY_FD_SET(fd, rdset);
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    return (1 == select(fd + 1, (fd_set *)rdset, NULL, NULL, &tv));
}

int
main(int argc, char **argv)
{
    int i, npass, ninit, nstep;
    unsigned long elapsed = 0;
    struct sockaddr_in saddr;

    if(5 != argc) {
        fprintf(stderr, "Usage: %s <npass> <nceil> <ninit> <nstep>\n\n"
                "<npass>    pass of calls of select/poll/epoll in a test round.\n"
                "<nceil>    max number of fds to handle.\n"
                "<ninit>    initial number of fds to handle.\n"
                "<nstep>    incremental number of fds for a next test.\n",
                argv[0]);
        exit(1);
    }
    npass = atoi(argv[1]);
    nceil = atoi(argv[2]);
    ninit = atoi(argv[3]);
    nstep = atoi(argv[4]);

    /* alloc memory */
    fds = calloc(nceil, sizeof(int));
    pfds = calloc(nceil, sizeof(struct pollfd));
    evs = calloc(nceil, sizeof(struct epoll_event));
    rdset = calloc(nceil, 8);
    assert(fds && pfds && evs && rdset);

    /* (nceil-1) sockets which have no data to receive */
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

    /* the only socket that data would be received from */
    saddr.sin_port = htons(12346);
    fds[i] = socket(AF_INET, SOCK_STREAM, 0);
    assert(fds[i] >= 0);
    assert(0 == connect(fds[i], (struct sockaddr *)&saddr, sizeof(saddr)));

    rdset_nbytes = MY_FD_SET_NBYTES(fds[i]);

    int round = 0;
    for(ncur = ninit; ncur <= nceil; ncur += nstep) {
        fprintf(stderr, "round with number of fds: %d\n", ncur);

        result[0][round] = ncur;

        elapsed = 0;
        for(i = 0; i < npass; i++) {
            while(!is_ready());
            elapsed += do_select();
        }
        result[1][round] = elapsed / npass;
        fprintf(stderr, "elapsed time of select(): %dus\n", elapsed / npass);

        elapsed = 0;
        for(i = 0; i < npass; i++) {
            while(!is_ready());
            elapsed += do_poll();
        }
        result[2][round] = elapsed / npass;
        fprintf(stderr, "elapsed time of poll(): %dus\n", elapsed / npass);

        elapsed = 0;
        for(i = 0; i < npass; i++) {
            while(!is_ready());
            elapsed += do_epoll();
        }
        result[3][round] = elapsed / npass;
        fprintf(stderr, "elapsed time of epoll(): %dus\n", elapsed / npass);

        round++;
    }

    printf("# #fds\tselect\tpoll\tepoll\n");
    for(i = 0; i < round; i++) {
        printf("%lu\t%lu\t%lu\t%lu\n", result[0][i], result[1][i], result[2][i], result[3][i]);
    }

    for(i = 0; i < nceil; i++) {
        close(fds[i]);
    }
    free(fds);
    free(pfds);
    free(evs);
    free(rdset);
}

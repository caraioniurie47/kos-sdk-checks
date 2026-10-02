/* More socket behaviour on KasperskyOS CE 1.4.0.102 (QEMU, aarch64) that differs from POSIX or Linux:
 * - sendmsg() on a TCP socket with more than 64 KiB. POSIX has a stream socket send part of what it is given; EMSGSIZE
 *   is for sockets that send messages atomically. Each call goes to a non-blocking loopback TCP socket whose peer has
 *   read nothing; send() and writev() of the same sizes are shown for comparison. Then 1 MiB through sendmsg() and
 *   send() on a blocking socket whose peer reads everything, and how much the peer read.
 * - setsockopt() on a TCP socket whose peer has reset the connection, and close() after it.
 * Runs with VfsNet as its network backend; en0 is configured first, as the SDK's network examples do. Every check
 * prints one "[check]" line. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include <kos_net.h>

static void result(const char* what, long rc)
{
    int e = errno;
    printf("[check] %-58s -> %ld%s%s\n", what, rc, rc < 0 ? ", errno " : "", rc < 0 ? strerror(e) : "");
}

/* A connected loopback TCP pair; the client is non-blocking. */
static int tcp_pair(int* client, int* server, struct sockaddr_in* peer)
{
    int l = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(a);
    if (bind(l, (struct sockaddr*)&a, sizeof(a)) || listen(l, 1) || getsockname(l, (struct sockaddr*)&a, &len))
        return -1;
    *client = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(*client, (struct sockaddr*)&a, sizeof(a)))
        return -1;
    *server = accept(l, NULL, NULL);
    close(l);
    fcntl(*client, F_SETFL, fcntl(*client, F_GETFL) | O_NONBLOCK);
    *peer = a;
    return *server < 0 ? -1 : 0;
}

/* how: 0 sendmsg, 1 sendmsg with the peer's address as msg_name, 2 send, 3 writev */
static void check(const char* what, int how, size_t length)
{
    static char data[1024 * 1024];
    int c, s;
    struct sockaddr_in peer;
    if (tcp_pair(&c, &s, &peer))
    {
        printf("[check] %-58s -> connection failed\n", what);
        return;
    }
    struct iovec iov = { data, length };
    struct msghdr m;
    memset(&m, 0, sizeof(m));
    m.msg_iov = &iov;
    m.msg_iovlen = 1;
    if (how == 1)
    {
        m.msg_name = &peer;
        m.msg_namelen = sizeof(peer);
    }
    errno = 0;
    long rc = how <= 1 ? (long)sendmsg(c, &m, 0) : how == 2 ? (long)send(c, data, length, 0) : (long)writev(c, &iov, 1);
    result(what, rc);
    close(c);
    close(s);
}

struct drain
{
    int fd;
    long total;
};

/* The peer reads until the connection closes. */
static void* drain(void* arg)
{
    static char sink[65536];
    struct drain* d = arg;
    long n;
    while ((n = read(d->fd, sink, sizeof(sink))) > 0)
        d->total += n;
    return NULL;
}

/* The same calls on a blocking socket whose peer reads everything: POSIX has a blocking sendmsg() wait for space. */
static void check_blocking(const char* what, int how, size_t length)
{
    static char data[1024 * 1024];
    int c, s;
    struct sockaddr_in peer;
    if (tcp_pair(&c, &s, &peer))
    {
        printf("[check] %-58s -> connection failed\n", what);
        return;
    }
    fcntl(c, F_SETFL, fcntl(c, F_GETFL) & ~O_NONBLOCK);
    struct drain d = { s, 0 };
    pthread_t t;
    pthread_create(&t, NULL, drain, &d);
    struct iovec iov = { data, length };
    struct msghdr m;
    memset(&m, 0, sizeof(m));
    m.msg_iov = &iov;
    m.msg_iovlen = 1;
    errno = 0;
    long rc = how == 0 ? (long)sendmsg(c, &m, 0) : (long)send(c, data, length, 0);
    result(what, rc);
    close(c);
    pthread_join(t, NULL);
    printf("[check] %-58s -> %ld\n", how == 0 ? "  bytes the peer read after sendmsg" : "  bytes the peer read after send",
           d.total);
    close(s);
}

/* The peer resets the connection (SO_LINGER {1, 0}, then close); then options are set on this end, and it is closed. */
static void reset_checks(void)
{
    int c, s;
    struct sockaddr_in peer;
    if (tcp_pair(&c, &s, &peer))
    {
        printf("[check] %-58s -> connection failed\n", "setsockopt after a reset");
        return;
    }
    struct linger zero = { 1, 0 };
    setsockopt(s, SOL_SOCKET, SO_LINGER, &zero, sizeof(zero));
    close(s);
    usleep(300000);
    errno = 0;
    result("setsockopt(SO_LINGER {1, 0}) after the peer reset", setsockopt(c, SOL_SOCKET, SO_LINGER, &zero, sizeof(zero)));
    int one = 1;
    errno = 0;
    result("setsockopt(SO_KEEPALIVE, 1) after the peer reset", setsockopt(c, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof(one)));
    errno = 0;
    result("close() after that", close(c));
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!wait_for_iface(DEFAULT_INTERFACE, IWF_EXISTS, DEFAULT_TIMEOUT) ||
        !configure_net_iface(DEFAULT_INTERFACE, DEFAULT_ADDR, DEFAULT_MASK, DEFAULT_GATEWAY, DEFAULT_MTU))
        printf("network setup failed\n");

    check("sendmsg(tcp, 65536 bytes)", 0, 65536);
    check("sendmsg(tcp, 65537 bytes)", 0, 65537);
    check("sendmsg(tcp, 1 MiB)", 0, 1024 * 1024);
    check("sendmsg(tcp, 1 MiB, msg_name = peer)", 1, 1024 * 1024);
    check("send(tcp, 1 MiB)", 2, 1024 * 1024);
    check("writev(tcp, 1 MiB)", 3, 1024 * 1024);
    check_blocking("sendmsg(blocking tcp, 1 MiB), the peer reading", 0, 1024 * 1024);
    check_blocking("send(blocking tcp, 1 MiB), the peer reading", 1, 1024 * 1024);
    reset_checks();

    printf("[check] done\n");
    return 0;
}

/* Socket behaviour on KasperskyOS CE 1.4.0.102 (QEMU, aarch64) that differs from POSIX or Linux, third set:
 * - connect() of a connected UDP socket to AF_UNSPEC, which POSIX says resets the peer, and to 0.0.0.0:0.
 * - recv() of zero bytes on an empty non-blocking TCP socket whose peer is open (POSIX: EAGAIN, not 0).
 * - FIONREAD with one 3-byte datagram, or 3 stream bytes, queued.
 * - A thread blocked in read() on TCP, or recvmsg() on UDP, while another thread closes the socket: what the blocked
 *   call returns within 5 s. errno is printed as a number too.
 * - SO_SNDBUF and SO_RCVBUF set to 0.
 * - Two UDP sockets bound to one 127.0.0.1 port, with SO_REUSEADDR and with SO_REUSEPORT. Where the headers do not
 *   define SO_REUSEPORT, NetBSD's value 0x0200 is used; a line says which.
 * - getsockopt(SO_RCVBUF) with option_len 0, with a NULL option_value and with a buffer.
 * - IOV_MAX, and UDP sendmsg() with 11, 1024 and 1025 one-byte iovecs (POSIX: EMSGSIZE above IOV_MAX).
 * - sendto() of zero bytes on a UDP socket, then of one byte: what the receiver's recvfrom() returns first.
 * - accept4() with an address buffer of 128 and of 244 bytes, on a TCP and on an AF_UNIX listener.
 * Runs with VfsNet as its network backend; en0 is configured first, as the SDK's network examples do. Every check
 * prints one "[check]" line. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

#include <kos_net.h>

#ifdef SO_REUSEPORT
#define REUSEPORT_DEFINED 1
#else
#define SO_REUSEPORT 0x0200
#define REUSEPORT_DEFINED 0
#endif

static void result(const char* what, long rc)
{
    int e = errno;
    printf("[check] %-58s -> %ld%s%s\n", what, rc, rc < 0 ? ", errno " : "", rc < 0 ? strerror(e) : "");
}

static void loopback(struct sockaddr_in* a)
{
    memset(a, 0, sizeof(*a));
    a->sin_family = AF_INET;
    a->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
}

/* A UDP socket bound to 127.0.0.1 and a free port; its address in *a. */
static int bound_udp(struct sockaddr_in* a)
{
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    loopback(a);
    bind(s, (struct sockaddr*)a, sizeof(*a));
    socklen_t len = sizeof(*a);
    getsockname(s, (struct sockaddr*)a, &len);
    return s;
}

/* A connected loopback TCP pair. */
static int tcp_pair(int* client, int* server)
{
    int l = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    loopback(&a);
    socklen_t len = sizeof(a);
    if (bind(l, (struct sockaddr*)&a, sizeof(a)) || listen(l, 1) || getsockname(l, (struct sockaddr*)&a, &len))
        return -1;
    *client = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(*client, (struct sockaddr*)&a, sizeof(a)))
        return -1;
    *server = accept(l, NULL, NULL);
    close(l);
    return *server < 0 ? -1 : 0;
}

static void wait_readable(int fd)
{
    struct pollfd p = { fd, POLLIN, 0 };
    poll(&p, 1, 1000);
}

static void udp_connect_checks(void)
{
    struct sockaddr_in peer;
    int p = bound_udp(&peer);
    int u = socket(AF_INET, SOCK_DGRAM, 0);
    errno = 0;
    result("UDP connect(127.0.0.1:<peer port>)", connect(u, (struct sockaddr*)&peer, sizeof(peer)));
    struct sockaddr unspec;
    memset(&unspec, 0, sizeof(unspec));
    unspec.sa_family = AF_UNSPEC;
    errno = 0;
    result("  then connect(AF_UNSPEC)", connect(u, &unspec, sizeof(unspec)));
    struct sockaddr_in zero;
    memset(&zero, 0, sizeof(zero));
    zero.sin_family = AF_INET;
    errno = 0;
    result("  then connect(0.0.0.0:0)", connect(u, (struct sockaddr*)&zero, sizeof(zero)));
    close(u);
    close(p);
}

static void zero_length_recv(void)
{
    int c, s;
    if (tcp_pair(&c, &s))
    {
        printf("[check] %-58s -> connection failed\n", "recv of zero bytes");
        return;
    }
    fcntl(c, F_SETFL, fcntl(c, F_GETFL) | O_NONBLOCK);
    char buf[1];
    errno = 0;
    result("recv(non-blocking TCP, 0 bytes), nothing queued", (long)recv(c, buf, 0, 0));
    close(c);
    close(s);
}

static void fionread(void)
{
    struct sockaddr_in a;
    int r = bound_udp(&a);
    int w = socket(AF_INET, SOCK_DGRAM, 0);
    sendto(w, "abc", 3, 0, (struct sockaddr*)&a, sizeof(a));
    wait_readable(r);
    int n = -1;
    ioctl(r, FIONREAD, &n);
    printf("[check] %-58s -> %d\n", "ioctl(UDP, FIONREAD), one 3-byte datagram queued", n);
    close(w);
    close(r);

    int c, s;
    if (tcp_pair(&c, &s) == 0)
    {
        send(s, "abc", 3, 0);
        wait_readable(c);
        n = -1;
        ioctl(c, FIONREAD, &n);
        printf("[check] %-58s -> %d\n", "ioctl(TCP, FIONREAD), 3 bytes queued", n);
        close(c);
        close(s);
    }
}

struct blocked
{
    int fd;
    int udp;
    volatile int done;
    long rc;
    int err;
};

static void* blocked_call(void* arg)
{
    struct blocked* b = arg;
    char buf[16];
    errno = 0;
    if (b->udp)
    {
        struct iovec iov = { buf, sizeof(buf) };
        struct msghdr m;
        memset(&m, 0, sizeof(m));
        m.msg_iov = &iov;
        m.msg_iovlen = 1;
        b->rc = (long)recvmsg(b->fd, &m, 0);
    }
    else
    {
        b->rc = (long)read(b->fd, buf, sizeof(buf));
    }
    b->err = errno;
    b->done = 1;
    return NULL;
}

/* The blocked thread is left behind if its call never returns. */
static void close_under_blocked_call(const char* what, int fd, int udp)
{
    static struct blocked b[2];
    struct blocked* x = &b[udp];
    x->fd = fd;
    x->udp = udp;
    pthread_t t;
    pthread_create(&t, NULL, blocked_call, x);
    sleep(1);
    close(fd);
    for (int i = 0; i < 50 && !x->done; i++)
        usleep(100000);
    if (!x->done)
        printf("[check] %-58s -> still blocked 5 s later\n", what);
    else if (x->rc < 0)
        printf("[check] %-58s -> %ld, errno %d (%s)\n", what, x->rc, x->err, strerror(x->err));
    else
        printf("[check] %-58s -> %ld\n", what, x->rc);
}

static void close_checks(void)
{
    int c, s;
    if (tcp_pair(&c, &s) == 0)
    {
        close_under_blocked_call("read(TCP) blocked, another thread closes the socket", c, 0);
        close(s);
    }
    struct sockaddr_in a;
    int u = bound_udp(&a);
    close_under_blocked_call("recvmsg(UDP) blocked, another thread closes the socket", u, 1);
}

static void buffer_size(const char* what, int type, int option)
{
    int s = socket(AF_INET, type, 0);
    int zero = 0;
    errno = 0;
    result(what, setsockopt(s, SOL_SOCKET, option, &zero, sizeof(zero)));
    close(s);
}

static void reuse(const char* what, int option)
{
    int one = 1;
    int a = socket(AF_INET, SOCK_DGRAM, 0);
    int b = socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(a, SOL_SOCKET, option, &one, sizeof(one));
    setsockopt(b, SOL_SOCKET, option, &one, sizeof(one));
    struct sockaddr_in addr;
    loopback(&addr);
    bind(a, (struct sockaddr*)&addr, sizeof(addr));
    socklen_t len = sizeof(addr);
    getsockname(a, (struct sockaddr*)&addr, &len);
    errno = 0;
    result(what, bind(b, (struct sockaddr*)&addr, sizeof(addr)));
    close(a);
    close(b);
}

static void getsockopt_len0(const char* what, int use_null)
{
    int s = socket(AF_INET, SOCK_STREAM, 0);
    int value = 0;
    socklen_t len = 0;
    errno = 0;
    result(what, getsockopt(s, SOL_SOCKET, SO_RCVBUF, use_null ? NULL : &value, &len));
    close(s);
}

static void iovecs(int count)
{
    struct sockaddr_in a;
    int s = bound_udp(&a);
    char* data = calloc((size_t)count, 1);
    struct iovec* iov = calloc((size_t)count, sizeof(struct iovec));
    for (int i = 0; i < count; i++)
    {
        iov[i].iov_base = data + i;
        iov[i].iov_len = 1;
    }
    struct msghdr m;
    memset(&m, 0, sizeof(m));
    m.msg_name = &a;
    m.msg_namelen = sizeof(a);
    m.msg_iov = iov;
    m.msg_iovlen = count;
    char what[64];
    snprintf(what, sizeof(what), "UDP sendmsg with %d one-byte iovecs", count);
    errno = 0;
    result(what, (long)sendmsg(s, &m, 0));
    free(iov);
    free(data);
    close(s);
}

static void empty_datagram(void)
{
    struct sockaddr_in a;
    int r = bound_udp(&a);
    int w = socket(AF_INET, SOCK_DGRAM, 0);
    char buf[4] = { 7 };
    errno = 0;
    result("UDP sendto(0 bytes)", (long)sendto(w, buf, 0, 0, (struct sockaddr*)&a, sizeof(a)));
    errno = 0;
    result("UDP sendto(1 byte)", (long)sendto(w, buf, 1, 0, (struct sockaddr*)&a, sizeof(a)));
    wait_readable(r);
    usleep(200000);
    errno = 0;
    result("  receiver's first recvfrom (0: the empty one)", (long)recvfrom(r, buf, sizeof(buf), MSG_DONTWAIT, NULL, NULL));
    errno = 0;
    result("  receiver's second recvfrom", (long)recvfrom(r, buf, sizeof(buf), MSG_DONTWAIT, NULL, NULL));
    close(w);
    close(r);
}

static void accept_tcp(socklen_t length)
{
    struct sockaddr_in a;
    loopback(&a);
    int l = socket(AF_INET, SOCK_STREAM, 0);
    bind(l, (struct sockaddr*)&a, sizeof(a));
    listen(l, 1);
    socklen_t al = sizeof(a);
    getsockname(l, (struct sockaddr*)&a, &al);
    int c = socket(AF_INET, SOCK_STREAM, 0);
    connect(c, (struct sockaddr*)&a, sizeof(a));
    union
    {
        struct sockaddr_storage aligned;
        unsigned char bytes[512];
    } buf;
    memset(&buf, 0, sizeof(buf));
    socklen_t len = length;
    char what[64];
    snprintf(what, sizeof(what), "accept4(TCP listener, address_len %u)", (unsigned)length);
    errno = 0;
    int s = accept4(l, (struct sockaddr*)&buf.aligned, &len, SOCK_CLOEXEC);
    result(what, s < 0 ? -1 : 0);
    if (s >= 0)
        close(s);
    close(c);
    close(l);
}

static void accept_unix(socklen_t length)
{
    struct sockaddr_un a;
    memset(&a, 0, sizeof(a));
    a.sun_family = AF_UNIX;
    snprintf(a.sun_path, sizeof(a.sun_path), "/tmp/net3-check-%u", (unsigned)length);
    unlink(a.sun_path);
    int l = socket(AF_UNIX, SOCK_STREAM, 0);
    bind(l, (struct sockaddr*)&a, sizeof(a));
    listen(l, 1);
    int c = socket(AF_UNIX, SOCK_STREAM, 0);
    connect(c, (struct sockaddr*)&a, sizeof(a));
    union
    {
        struct sockaddr_storage aligned;
        unsigned char bytes[512];
    } buf;
    memset(&buf, 0, sizeof(buf));
    socklen_t len = length;
    char what[64];
    snprintf(what, sizeof(what), "accept4(AF_UNIX listener, address_len %u)", (unsigned)length);
    errno = 0;
    int s = accept4(l, (struct sockaddr*)&buf.aligned, &len, SOCK_CLOEXEC);
    result(what, s < 0 ? -1 : 0);
    if (s >= 0)
        close(s);
    close(c);
    close(l);
    unlink(a.sun_path);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!wait_for_iface(DEFAULT_INTERFACE, IWF_EXISTS, DEFAULT_TIMEOUT) ||
        !configure_net_iface(DEFAULT_INTERFACE, DEFAULT_ADDR, DEFAULT_MASK, DEFAULT_GATEWAY, DEFAULT_MTU))
        printf("network setup failed\n");

    udp_connect_checks();
    zero_length_recv();
    fionread();
    close_checks();
    buffer_size("setsockopt(TCP, SO_SNDBUF, 0)", SOCK_STREAM, SO_SNDBUF);
    buffer_size("setsockopt(TCP, SO_RCVBUF, 0)", SOCK_STREAM, SO_RCVBUF);
    buffer_size("setsockopt(UDP, SO_SNDBUF, 0)", SOCK_DGRAM, SO_SNDBUF);
    reuse("second UDP bind to one port, SO_REUSEADDR on both", SO_REUSEADDR);
    printf("[check] %-58s -> %s\n", "SO_REUSEPORT defined by <sys/socket.h>", REUSEPORT_DEFINED ? "yes" : "no");
    reuse("second UDP bind to one port, SO_REUSEPORT on both", SO_REUSEPORT);
    getsockopt_len0("getsockopt(SO_RCVBUF, NULL, option_len 0)", 1);
    getsockopt_len0("getsockopt(SO_RCVBUF, buffer, option_len 0)", 0);
    printf("[check] %-58s -> %d\n", "IOV_MAX", (int)IOV_MAX);
    iovecs(11);
    iovecs(1024);
    iovecs(1025);
    empty_datagram();
    accept_tcp(128);
    accept_tcp(244);
    accept_unix(128);
    accept_unix(244);

    printf("[check] done\n");
    return 0;
}

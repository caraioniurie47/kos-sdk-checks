/* Socket behaviour on KasperskyOS CE 1.4.0.102 (QEMU, aarch64) that differs from POSIX or Linux.
 * Runs with VfsNet as its network backend and VfsRamFs as its file system; en0 is configured first, as the SDK's
 * network examples do. Every check prints one "[check]" line. */
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#include <kos_net.h>

static void result(const char* what, long rc)
{
    int e = errno;
    printf("[check] %-58s -> %ld%s%s\n", what, rc, rc < 0 ? ", errno " : "", rc < 0 ? strerror(e) : "");
}

/* A connected loopback TCP pair; the listener is non-blocking when nonblockingListener is set. */
static int tcp_pair(int* client, int* server, int nonblockingListener)
{
    int l = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a;
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof(a);
    if (bind(l, (struct sockaddr*)&a, sizeof(a)) || listen(l, 1) || getsockname(l, (struct sockaddr*)&a, &len))
        return -1;
    if (nonblockingListener)
        fcntl(l, F_SETFL, fcntl(l, F_GETFL) | O_NONBLOCK);
    *client = socket(AF_INET, SOCK_STREAM, 0);
    if (connect(*client, (struct sockaddr*)&a, sizeof(a)))
        return -1;
    struct pollfd p = { l, POLLIN, 0 };
    poll(&p, 1, 3000);
    *server = accept4(l, NULL, NULL, SOCK_CLOEXEC);
    close(l);
    return *server < 0 ? -1 : 0;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!wait_for_iface(DEFAULT_INTERFACE, IWF_EXISTS, DEFAULT_TIMEOUT) ||
        !configure_net_iface(DEFAULT_INTERFACE, DEFAULT_ADDR, DEFAULT_MASK, DEFAULT_GATEWAY, DEFAULT_MTU))
        printf("network setup failed\n");

    /* IPv6 */
    errno = 0;
    result("socket(AF_INET6, SOCK_STREAM, 0)", socket(AF_INET6, SOCK_STREAM, 0));

    /* Name resolution without a hosts file for VfsNet */
    struct addrinfo hints, *res = NULL;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    int gai = getaddrinfo("localhost", NULL, &hints, &res);
    printf("[check] %-58s -> %d%s%s\n", "getaddrinfo(\"localhost\")", gai, gai ? ", " : "", gai ? gai_strerror(gai) : "");
    if (res)
        freeaddrinfo(res);

    /* The same with an /etc/hosts in the program's own file system (VfsRamFs). Not on Linux, where /etc/hosts is the
     * system's. */
#ifdef __KOS__
    errno = 0;
    result("  mkdir(\"/etc\") in the program's file system", mkdir("/etc", 0755));
    errno = 0;
    int hosts = open("/etc/hosts", O_CREAT | O_WRONLY | O_TRUNC, 0644);
    long wrote = hosts;
    if (hosts >= 0)
    {
        errno = 0;
        wrote = (long)write(hosts, "127.0.0.1 localhost\n", 20);
        close(hosts);
    }
    result("  then /etc/hosts created, bytes of 127.0.0.1 localhost", wrote);
    res = NULL;
    gai = getaddrinfo("localhost", NULL, &hints, &res);
    printf("[check] %-58s -> %d%s%s\n", "  again, with /etc/hosts in the program's file system", gai, gai ? ", " : "",
           gai ? gai_strerror(gai) : "");
    if (res)
        freeaddrinfo(res);
#else
    printf("[check] %-58s -> not run\n", "  mkdir(\"/etc\") in the program's file system");
    printf("[check] %-58s -> not run\n", "  then /etc/hosts created, bytes of 127.0.0.1 localhost");
    printf("[check] %-58s -> not run\n", "  again, with /etc/hosts in the program's file system");
#endif

    int c, s;
    char buf[81920];
    memset(buf, 'x', sizeof(buf));

    /* sendfile from a regular file to a connected TCP socket */
    if (tcp_pair(&c, &s, 0) == 0)
    {
        int fd = open("/tmp/data.bin", O_CREAT | O_RDWR | O_TRUNC, 0600);
        write(fd, buf, 1024);
        off_t offset = 0;
        errno = 0;
        result("sendfile(tcp socket, file, &offset, 512)", (long)sendfile(c, fd, &offset, 512));
        close(fd);

        /* recv into a buffer larger than 64 KiB, with more than 64 KiB waiting */
        long sent = 0;
        while (sent < 70000)
            sent += (long)write(c, buf, (size_t)(70000 - sent));
        usleep(500000);
        errno = 0;
        result("recv(tcp socket, 81920-byte buffer), 70000 bytes sent", (long)recv(s, buf, sizeof(buf), MSG_DONTWAIT));
        errno = 0;
        result("recv(tcp socket, 65536-byte buffer) after that", (long)recv(s, buf, 65536, MSG_DONTWAIT));
        close(c);
        close(s);
    }

    /* shutdown of a socket that is not connected (POSIX: ENOTCONN) */
    int u = socket(AF_INET, SOCK_STREAM, 0);
    errno = 0;
    result("shutdown(unconnected TCP socket, SHUT_RDWR)", shutdown(u, SHUT_RDWR));
    close(u);

    /* The same on a bound UDP socket, then what a receive sees afterwards (Linux: ENOTCONN, then EAGAIN) */
    u = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in any;
    memset(&any, 0, sizeof(any));
    any.sin_family = AF_INET;
    any.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(u, (struct sockaddr*)&any, sizeof(any)) == 0)
    {
        errno = 0;
        result("shutdown(bound unconnected UDP socket, SHUT_RDWR)", shutdown(u, SHUT_RDWR));
        struct pollfd up = { u, POLLIN, 0 };
        errno = 0;
        result("  then poll(POLLIN, 0)", poll(&up, 1, 0));
        printf("[check] %-58s -> 0x%x\n", "  then revents", up.revents);
        char one;
        struct iovec iov = { &one, 1 };
        struct msghdr m;
        memset(&m, 0, sizeof(m));
        m.msg_iov = &iov;
        m.msg_iovlen = 1;
        errno = 0;
        result("  then recvmsg(1-byte buffer, MSG_DONTWAIT)", (long)recvmsg(u, &m, MSG_DONTWAIT));
    }
    close(u);

    /* O_NONBLOCK of a socket accepted from a non-blocking listener (Linux: not inherited) */
    if (tcp_pair(&c, &s, 1) == 0)
    {
        printf("[check] %-58s -> %s\n", "accept4(non-blocking listener, SOCK_CLOEXEC): O_NONBLOCK",
               (fcntl(s, F_GETFL) & O_NONBLOCK) ? "set" : "clear");
        close(c);
        close(s);
    }

    /* poll() with one closed descriptor among open ones (POSIX: POLLNVAL in that entry) */
    int a = socket(AF_INET, SOCK_STREAM, 0), b = socket(AF_INET, SOCK_STREAM, 0);
    close(b);
    struct pollfd pf[2] = { { a, POLLOUT, 0 }, { b, POLLOUT, 0 } };
    errno = 0;
    int pr = poll(pf, 2, 0);
    result("poll({open socket, closed descriptor}, 2, 0)", pr);
    printf("[check] %-58s -> 0x%x, 0x%x\n", "  revents", pf[0].revents, pf[1].revents);
    close(a);

    /* poll() with more than 512 entries, all -1 (POSIX: ignored entries) */
    static struct pollfd many[513];
    for (int i = 0; i < 513; i++)
    {
        many[i].fd = -1;
        many[i].events = POLLIN;
    }
    errno = 0;
    result("poll(513 entries, every fd -1, timeout 0)", poll(many, 513, 0));
    errno = 0;
    result("poll(512 entries, every fd -1, timeout 0)", poll(many, 512, 0));

    printf("[check] done\n");
    return 0;
}

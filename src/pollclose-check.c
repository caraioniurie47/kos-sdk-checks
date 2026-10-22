/* poll() while another thread closes one of the polled descriptors, as an event loop meets when a socket is closed
 * during its wait. A poller thread copies the descriptor table under a mutex and polls the copy for up to 10 ms without
 * the mutex; the main thread replaces table entries, closing the old socket at once, so a close can land inside a
 * poll() that holds it. Counts every poll() result by kind; prints them at the end (never from the polling thread). */

#include <errno.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

enum { NFDS = 8, CLOSES = 3000 };

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_fds[NFDS];
static volatile int g_stop = 0;

static long g_calls = 0;
static long g_ok = 0;
static long g_nval = 0;
static long g_ebadf = 0;
static long g_einval = 0;
static long g_eacces = 0;
static long g_other = 0;
static int g_otherErrno = 0;

static int NewSocket(void)
{
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0)
    {
        struct sockaddr_in a;
        memset(&a, 0, sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = 0;
        (void)bind(fd, (struct sockaddr*)&a, sizeof(a));
    }
    return fd;
}

static void* Poller(void* arg)
{
    (void)arg;
    struct pollfd p[NFDS];
    while (!g_stop)
    {
        pthread_mutex_lock(&g_lock);
        for (int i = 0; i < NFDS; i++)
        {
            p[i].fd = g_fds[i];
            p[i].events = POLLIN;
            p[i].revents = 0;
        }
        pthread_mutex_unlock(&g_lock);
        int r = poll(p, NFDS, 10);
        g_calls++;
        if (r >= 0)
        {
            g_ok++;
            for (int i = 0; i < NFDS; i++)
            {
                if ((p[i].revents & POLLNVAL) != 0)
                {
                    g_nval++;
                }
            }
        }
        else if (errno == EBADF)
        {
            g_ebadf++;
        }
        else if (errno == EINVAL)
        {
            g_einval++;
        }
        else if (errno == EACCES)
        {
            g_eacces++;
        }
        else
        {
            g_other++;
            g_otherErrno = errno;
        }
    }
    return NULL;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    for (int i = 0; i < NFDS; i++)
    {
        g_fds[i] = NewSocket();
    }
    printf("[check] %d UDP sockets, first fd %d, last fd %d\n", NFDS, g_fds[0], g_fds[NFDS - 1]);

    pthread_t t;
    int rc = pthread_create(&t, NULL, Poller, NULL);
    printf("[check] pthread_create -> %d\n", rc);

    struct timespec pause = { 0, 1000000 }; // 1 ms
    int closes = 0;
    int failedCloses = 0;
    for (int n = 0; n < CLOSES; n++)
    {
        int i = n % NFDS;
        int fresh = NewSocket();
        pthread_mutex_lock(&g_lock);
        int old = g_fds[i];
        g_fds[i] = fresh;
        pthread_mutex_unlock(&g_lock);
        if (close(old) == 0)
        {
            closes++;
        }
        else
        {
            failedCloses++;
        }
        nanosleep(&pause, NULL);
        if (n % 500 == 499)
        {
            printf("[check] progress: %d closes\n", n + 1);
        }
    }
    g_stop = 1;
    pthread_join(t, NULL);
    printf("[check] closes while polling: %d (close failed %d)\n", closes, failedCloses);
    printf("[check] poll calls %ld: returned >= 0 %ld (POLLNVAL entries %ld); -1 EBADF %ld, EINVAL %ld, EACCES %ld,"
           " other %ld (last errno %d)\n",
           g_calls, g_ok, g_nval, g_ebadf, g_einval, g_eacces, g_other, g_otherErrno);
    printf("[check] done\n");
    return 0;
}

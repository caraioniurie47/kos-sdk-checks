/* posix_fallocate() on VfsRamFs for growing lengths, 1 MiB to 1 TiB, each on a new file in /tmp, timed, with the
 * system's free pages before and after; a watcher thread prints free pages every 5 s while a call runs, so a slow fill
 * (pages falling) shows apart from a stall (pages flat). ftruncate to 1 TiB first, as fs-check does after
 * posix_fallocate. Every setup call prints its result. */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <coresrv/stat/stat_api.h>

static volatile int running;
static volatile long long started;

static long long free_pages(void)
{
    rtl_int64_t v = -1;
    KnGroupStatGetParam(GROUP_PARAM_MEM_FREE, &v);
    return (long long)v;
}

static long long now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static void* watcher(void* arg)
{
    (void)arg;
    for (;;)
    {
        sleep(5);
        if (running)
        {
            printf("[check]     ... still in the call after %lld ms, free pages %lld\n", now_ms() - started,
                   free_pages());
        }
    }
    return NULL;
}

static void one(const char* what, off_t len, int truncate)
{
    errno = 0;
    int fd = open("/tmp/big.bin", O_CREAT | O_RDWR | O_TRUNC, 0600);
    printf("[check] %s: open -> %d%s%s\n", what, fd, fd < 0 ? ", errno " : "", fd < 0 ? strerror(errno) : "");
    if (fd < 0)
    {
        return;
    }
    long long before = free_pages();
    printf("[check] %s: calling, free pages %lld\n", what, before);
    started = now_ms();
    running = 1;
    errno = 0;
    int rc = truncate ? ftruncate(fd, len) : posix_fallocate(fd, 0, len);
    int e = errno;
    running = 0;
    printf("[check] %s -> %d (errno %d %s), %lld ms, free pages %lld -> %lld\n", what, rc, e, strerror(e),
           now_ms() - started, before, free_pages());
    close(fd);
    errno = 0;
    int u = unlink("/tmp/big.bin");
    printf("[check] %s: unlink -> %d, free pages %lld\n", what, u, free_pages());
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    pthread_t t;
    int pc = pthread_create(&t, NULL, watcher, NULL);
    printf("[check] watcher thread: pthread_create -> %d\n", pc);

    one("ftruncate 1 TiB", (off_t)1 << 40, 1);
    one("posix_fallocate 1 MiB", (off_t)1 << 20, 0);
    one("posix_fallocate 64 MiB", (off_t)64 << 20, 0);
    one("posix_fallocate 512 MiB", (off_t)512 << 20, 0);
    one("posix_fallocate 4 GiB", (off_t)4 << 30, 0);
    one("posix_fallocate 1 TiB", (off_t)1 << 40, 0);
    printf("[check] done\n");
    return 0;
}

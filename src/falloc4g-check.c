/* posix_fallocate() of 4 GiB on VfsRamFs, more than the QEMU machine's RAM: what it returns, what fstat and statvfs
 * say afterwards, and a write at the end of the range. A watcher thread prints free pages every 30 s while the call
 * runs. Run with CHECK_TIMEOUT=3000 (seconds): on SDK 2.0.0.179 the call took 48 minutes. */
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
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
        sleep(30);
        if (running)
        {
            printf("[check]     ... still in the call after %lld ms, free pages %lld\n", now_ms() - started,
                   free_pages());
        }
    }
    return NULL;
}

static void show_fs(const char* when)
{
    struct statvfs v;
    errno = 0;
    int rc = statvfs("/tmp", &v);
    printf("[check] statvfs(/tmp) %s -> %d, f_blocks %llu, f_bfree %llu, f_bavail %llu, f_frsize %lu\n", when, rc,
           (unsigned long long)v.f_blocks, (unsigned long long)v.f_bfree, (unsigned long long)v.f_bavail,
           (unsigned long)v.f_frsize);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    pthread_t t;
    int pc = pthread_create(&t, NULL, watcher, NULL);
    printf("[check] watcher thread: pthread_create -> %d\n", pc);
    printf("[check] free pages at start: %lld\n", free_pages());
    show_fs("before");

    errno = 0;
    int fd = open("/tmp/big.bin", O_CREAT | O_RDWR | O_TRUNC, 0600);
    printf("[check] open -> %d%s%s\n", fd, fd < 0 ? ", errno " : "", fd < 0 ? strerror(errno) : "");
    if (fd < 0)
    {
        return 1;
    }
    long long before = free_pages();
    started = now_ms();
    running = 1;
    errno = 0;
    int rc = posix_fallocate(fd, 0, (off_t)4 << 30);
    int e = errno;
    running = 0;
    printf("[check] posix_fallocate(fd, 0, 4 GiB) -> %d (errno %d %s), %lld ms, free pages %lld -> %lld\n", rc, e,
           strerror(e), now_ms() - started, before, free_pages());

    struct stat st;
    errno = 0;
    int sr = fstat(fd, &st);
    printf("[check] fstat -> %d, st_size %lld, st_blocks %lld, st_blksize %ld\n", sr, (long long)st.st_size,
           (long long)st.st_blocks, (long)st.st_blksize);
    show_fs("after");

    char buf[4096];
    memset(buf, 0x5a, sizeof buf);
    errno = 0;
    off_t pos = lseek(fd, ((off_t)4 << 30) - (off_t)sizeof buf, SEEK_SET);
    printf("[check] lseek(4 GiB - 4 KiB) -> %lld\n", (long long)pos);
    errno = 0;
    long w = (long)write(fd, buf, sizeof buf);
    printf("[check] write(4 KiB at the end of the allocated range) -> %ld%s%s, free pages %lld\n", w,
           w < 0 ? ", errno " : "", w < 0 ? strerror(errno) : "", free_pages());
    close(fd);
    errno = 0;
    int u = unlink("/tmp/big.bin");
    printf("[check] unlink -> %d, free pages %lld\n", u, free_pages());
    printf("[check] done\n");
    return 0;
}

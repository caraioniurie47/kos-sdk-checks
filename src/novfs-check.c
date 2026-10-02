/* A program linked with the VFS client (vfs::client) in an image that has no VFS program, on KasperskyOS CE 1.4.0.102
 * (QEMU, aarch64): its C runtime waits for a VFS server before main() and then falls back to a stub. The wait shows in
 * the timestamps of the runtime's own log lines, which host/run-check.sh prints for this check; the lines below show
 * what the program sees afterwards, if its output reaches the console at all. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void result(const char* what, long rc)
{
    int e = errno;
    printf("[check] %-58s -> %ld%s%s\n", what, rc, rc < 0 ? ", errno " : "", rc < 0 ? strerror(e) : "");
}

int main(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("[check] %-58s -> %ld ms\n", "CLOCK_MONOTONIC on entry to main()", (long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
    errno = 0;
    result("open(\"/tmp/x\", O_CREAT | O_RDWR)", open("/tmp/x", O_CREAT | O_RDWR, 0600));
    printf("[check] done\n");
    return 0;
}

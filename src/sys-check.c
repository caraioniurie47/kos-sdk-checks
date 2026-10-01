/* Library and memory behaviour on KasperskyOS CE 1.4.0.102 (QEMU, aarch64) that differs from Linux or is undocumented:
 * - pthread_condattr_init() on zeroed memory, on an initialized attribute, on a byte copy of one, and after destroy.
 *   POSIX leaves re-initializing an initialized attribute undefined; a stack variable that reuses such bytes looks
 *   initialized, too.
 * - sysconf() of _SC_PHYS_PAGES (defined by unistd.h), _SC_PAGESIZE, _SC_NPROCESSORS_ONLN and _SC_OPEN_MAX.
 * - readdir() on a directory stream whose directory was removed with rmdir() after opendir().
 * - mprotect() on the page of a read-only (.rodata) variable: lowering to PROT_NONE and back, and raising to
 *   PROT_READ|PROT_WRITE and back; an anonymous mapping's page for comparison. POSIX leaves mprotect() unspecified for memory not
 *   mapped by mmap(), and lists EACCES for a protection the underlying object does not allow.
 * - getrlimit() and setrlimit() of RLIMIT_NOFILE (POSIX.1-2024 Base; XSI before), each followed by how many descriptors
 *   open() of a /tmp file then gives (at the start /dev/null too: its errno at the limit differs on KasperskyOS): a
 *   soft limit of 256 (lowering, as any process may), then 1024 for both limits (raising the hard limit needs
 *   privileges; run as root on Linux).
 * Every check prints one "[check]" line. */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

static void result(const char* what, long rc, int err)
{
    printf("[check] %-58s -> %ld%s%s\n", what, rc, err ? ", errno " : "", err ? strerror(err) : "");
}

static void condattrs(void)
{
    pthread_condattr_t a;
    memset(&a, 0, sizeof(a));
    int rc = pthread_condattr_init(&a);
    result("pthread_condattr_init(zeroed memory)", rc, rc);
    rc = pthread_condattr_init(&a);
    result("pthread_condattr_init(that attribute again)", rc, rc);
    pthread_condattr_t b;
    memcpy(&b, &a, sizeof(b));
    rc = pthread_condattr_init(&b);
    result("pthread_condattr_init(a byte copy of it)", rc, rc);
    pthread_condattr_destroy(&a);
    rc = pthread_condattr_init(&a);
    result("pthread_condattr_init(after pthread_condattr_destroy)", rc, rc);
    pthread_condattr_destroy(&a);
}

static void sysconfs(void)
{
    static const struct
    {
        const char* name;
        int id;
    } names[] = {
        { "sysconf(_SC_PHYS_PAGES)", _SC_PHYS_PAGES },
        { "sysconf(_SC_PAGESIZE)", _SC_PAGESIZE },
        { "sysconf(_SC_NPROCESSORS_ONLN)", _SC_NPROCESSORS_ONLN },
        { "sysconf(_SC_OPEN_MAX)", _SC_OPEN_MAX },
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
    {
        errno = 0;
        long v = sysconf(names[i].id);
        result(names[i].name, v, v < 0 ? errno : 0);
    }
}

static void removed_directory(void)
{
    rmdir("/tmp/sys-check-gone");
    mkdir("/tmp/sys-check-gone", 0700);
    DIR* d = opendir("/tmp/sys-check-gone");
    if (!d)
    {
        result("opendir(/tmp/sys-check-gone)", -1, errno);
        return;
    }
    int rc = rmdir("/tmp/sys-check-gone");
    result("rmdir of that directory while the stream is open", rc, rc ? errno : 0);
    errno = 0;
    struct dirent* e = readdir(d);
    int err = errno;
    if (e)
        printf("[check] %-58s -> an entry, \"%s\"\n", "  then readdir()", e->d_name);
    else
        printf("[check] %-58s -> NULL%s%s\n", "  then readdir()", err ? ", errno " : " (end, errno unchanged)",
               err ? strerror(err) : "");
    closedir(d);
}

__attribute__((section(".rodata"))) const volatile uint64_t read_only_value = 0;

/* Changes the page's protection to `first`, then to `second`, and prints both results only afterwards: while the page
 * of a .rodata variable is PROT_NONE, printf's format strings on it may be unreadable. */
static void protect_pair(const char* what, void* address, int first, int second)
{
    long page = sysconf(_SC_PAGESIZE);
    void* start = (void*)((uintptr_t)address & ~(uintptr_t)(page - 1));
    errno = 0;
    int rc1 = mprotect(start, (size_t)page, first);
    int e1 = errno;
    errno = 0;
    int rc2 = mprotect(start, (size_t)page, second);
    int e2 = errno;
    printf("[check] %-58s -> %d%s%s, then %d%s%s\n", what, rc1, rc1 ? ", errno " : "", rc1 ? strerror(e1) : "", rc2,
           rc2 ? ", errno " : "", rc2 ? strerror(e2) : "");
}

static void protections(void)
{
    void* ro = (void*)(uintptr_t)&read_only_value;
    protect_pair("mprotect(.rodata page, PROT_NONE), then PROT_READ", ro, PROT_NONE, PROT_READ);
    protect_pair("mprotect(.rodata page, PROT_READ|PROT_WRITE), then PROT_READ", ro, PROT_READ | PROT_WRITE, PROT_READ);
    long page = sysconf(_SC_PAGESIZE);
    void* anon = mmap(NULL, (size_t)page, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (anon == MAP_FAILED)
    {
        result("mmap(one page, PROT_READ)", -1, errno);
        return;
    }
    protect_pair("mprotect(anonymous page, PROT_READ|PROT_WRITE), then PROT_READ", anon, PROT_READ | PROT_WRITE, PROT_READ);
    munmap(anon, (size_t)page);
}

/* Opens `path` until open() fails, closes them all again, and prints how many opened. */
static void count_descriptors(const char* what, const char* path)
{
    enum { MAX_FDS = 5000 };
    static int fds[MAX_FDS];
    int n = 0;
    int err = 0;
    while (n < MAX_FDS)
    {
        int fd = open(path, O_RDONLY);
        if (fd < 0)
        {
            err = errno;
            break;
        }
        fds[n++] = fd;
    }
    for (int i = 0; i < n; i++)
        close(fds[i]);
    result(what, n, err);
}

static void descriptor_limits(void)
{
    const char* file = "/tmp/sys-check-limit";
    close(open(file, O_CREAT | O_WRONLY, 0600));
    struct rlimit rl;
    int rc = getrlimit(RLIMIT_NOFILE, &rl);
    result("getrlimit(RLIMIT_NOFILE)", rc, rc ? errno : 0);
    count_descriptors("  open() of a /tmp file until it fails", file);
    count_descriptors("  open() of /dev/null until it fails", "/dev/null");
    struct rlimit low = { 256, rc == 0 ? rl.rlim_max : 256 };
    rc = setrlimit(RLIMIT_NOFILE, &low);
    result("setrlimit(RLIMIT_NOFILE, soft limit 256)", rc, rc ? errno : 0);
    count_descriptors("  then open() of a /tmp file until it fails", file);
    struct rlimit high = { 1024, 1024 };
    rc = setrlimit(RLIMIT_NOFILE, &high);
    result("setrlimit(RLIMIT_NOFILE, soft and hard limit 1024)", rc, rc ? errno : 0);
    count_descriptors("  then open() of a /tmp file until it fails", file);
    unlink(file);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    condattrs();
    sysconfs();
    removed_directory();
    protections();
    descriptor_limits();
    printf("[check] done\n");
    return 0;
}

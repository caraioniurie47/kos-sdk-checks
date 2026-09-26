/* Address space reservation and release on KasperskyOS CE 1.4.0.102 (QEMU, aarch64, the SDK's -m 2048).
 * Free physical memory from KnGroupStatGetParam(GROUP_PARAM_MEM_FREE), in pages, before and after each step.
 * Then write-and-execute mappings: R|W|X at once, and R|W then R|X with code that is run (aarch64 instructions). */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include <coresrv/stat/stat_api.h>

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

static void reserve(const char* what, size_t len, int flags)
{
    long long before = free_pages(), t0 = now_ms();
    void* p = mmap(NULL, len, PROT_NONE, flags, -1, 0);
    long long ms = now_ms() - t0, after = free_pages();
    printf("[check] %-44s -> %s, %lld ms, free pages %lld -> %lld (%lld MiB taken)\n", what,
           p == MAP_FAILED ? strerror(errno) : "ok", ms, before, after, (before - after) * sysconf(_SC_PAGESIZE) / (1 << 20));
    if (p != MAP_FAILED)
        munmap(p, len);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const size_t MiB = 1 << 20;

    reserve("mmap(512 MiB, PROT_NONE, private anon)", 512 * MiB, MAP_PRIVATE | MAP_ANON);
    reserve("mmap(512 MiB, PROT_NONE, ... | MAP_NORESERVE)", 512 * MiB, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE);

    /* Commit 64 MiB, touch it, then try to give it back without unmapping. */
    size_t len = 64 * MiB;
    unsigned char* p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
    memset(p, 1, len);
    long long touched = free_pages();
    int rc = madvise(p, len, MADV_DONTNEED);
    printf("[check] %-44s -> %d, free pages %lld -> %lld\n", "madvise(64 MiB touched, MADV_DONTNEED)", rc, touched, free_pages());
    printf("[check] %-44s -> %d (want 0 after MADV_DONTNEED)\n", "  first byte afterwards", p[0]);
    touched = free_pages();
    rc = madvise(p, len, MADV_FREE);
    printf("[check] %-44s -> %d, free pages %lld -> %lld\n", "madvise(the same 64 MiB, MADV_FREE)", rc, touched, free_pages());
    long long before = free_pages();
    rc = munmap(p, len);
    printf("[check] %-44s -> %d, free pages %lld -> %lld\n", "munmap(the same 64 MiB)", rc, before, free_pages());

    /* Write and execute at once (W+X), as code generators map their stubs, then the W^X alternative:
     * write while R|W, switch to R|X, run the code. */
    size_t xlen = 64 * 1024;
    errno = 0;
    void* x = mmap(NULL, xlen, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    printf("[check] %-44s -> %s\n", "mmap(64 KiB, R|W|X, private anon)", x == MAP_FAILED ? strerror(errno) : "ok");
    if (x != MAP_FAILED)
        munmap(x, xlen);
    x = mmap(NULL, xlen, PROT_READ | PROT_EXEC, MAP_PRIVATE | MAP_ANON, -1, 0);
    errno = 0;
    rc = mprotect(x, xlen, PROT_READ | PROT_WRITE | PROT_EXEC);
    printf("[check] %-44s -> %d%s%s\n", "mprotect(64 KiB R|X anon, to R|W|X)", rc, rc != 0 ? ", errno " : "",
           rc != 0 ? strerror(errno) : "");
    munmap(x, xlen);
    unsigned int* code = mmap(NULL, xlen, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    code[0] = 0x52800540; /* mov w0, #42 */
    code[1] = 0xD65F03C0; /* ret */
    errno = 0;
    rc = mprotect(code, xlen, PROT_READ | PROT_EXEC);
    printf("[check] %-44s -> %d%s%s\n", "mprotect(R|W anon, code written, to R|X)", rc, rc != 0 ? ", errno " : "",
           rc != 0 ? strerror(errno) : "");
    if (rc == 0)
    {
        __builtin___clear_cache((char*)code, (char*)(code + 2));
        int (*fn)(void) = (int (*)(void))(void*)code;
        printf("[check] %-44s -> %d\n", "  calling that code (mov w0, #42; ret)", fn());
    }
    munmap(code, xlen);

    printf("[check] done\n");
    return 0;
}

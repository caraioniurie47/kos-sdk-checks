/* What happens on KasperskyOS CE 1.4.0.102 (QEMU, aarch64, the image's 2 GiB) when a process asks for more memory than
 * is free, in 256 MiB steps, writing every page of each step, up to 4 GiB:
 * 1. mmap(PROT_READ|PROT_WRITE, private anonymous) per step; all of it unmapped again afterwards.
 * 2. As a garbage collector commits memory: mmap(PROT_NONE, private anonymous | MAP_NORESERVE) per step, which takes
 *    (almost) no memory (mem-check.c), then mprotect(PROT_READ|PROT_WRITE) and the writes.
 * Free physical memory from KnGroupStatGetParam(GROUP_PARAM_MEM_FREE), in pages. Each step's line is printed before its
 * write starts, so the last line shows where the process was. host/run-check.sh also stops on the kernel's
 * "Unhandled Overcommit". KasperskyOS only. */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <coresrv/stat/stat_api.h>

#define STEPS 16

static long long free_pages(void)
{
    rtl_int64_t v = -1;
    KnGroupStatGetParam(GROUP_PARAM_MEM_FREE, &v);
    return (long long)v;
}

static void write_pages(char* p, size_t length)
{
    long page = sysconf(_SC_PAGESIZE);
    for (size_t off = 0; off < length; off += (size_t)page)
        p[off] = 1;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    const size_t step = (size_t)256 << 20;
    long page = sysconf(_SC_PAGESIZE);
    printf("[check] free pages at start: %lld (%lld MiB)\n", free_pages(), free_pages() * page / (1 << 20));

    char* mapped[STEPS];
    int count = 0;
    for (; count < STEPS; count++)
    {
        char* p = mmap(NULL, step, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
        if (p == MAP_FAILED)
        {
            printf("[check] 1. mmap(256 MiB, R|W) #%d -> %s, free pages %lld\n", count + 1, strerror(errno), free_pages());
            break;
        }
        mapped[count] = p;
        printf("[check] 1. mmap(256 MiB, R|W) #%d -> ok, free pages %lld; writing it\n", count + 1, free_pages());
        write_pages(p, step);
    }
    for (int i = 0; i < count; i++)
        munmap(mapped[i], step);
    printf("[check] 1. all unmapped, free pages %lld\n", free_pages());

    for (int i = 1; i <= STEPS; i++)
    {
        char* p = mmap(NULL, step, PROT_NONE, MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
        if (p == MAP_FAILED)
        {
            printf("[check] 2. mmap(256 MiB, PROT_NONE, MAP_NORESERVE) #%d -> %s\n", i, strerror(errno));
            break;
        }
        if (mprotect(p, step, PROT_READ | PROT_WRITE) != 0)
        {
            printf("[check] 2. mprotect(256 MiB, R|W) #%d -> %s, free pages %lld\n", i, strerror(errno), free_pages());
            break;
        }
        printf("[check] 2. reserve, mprotect(R|W) #%d -> ok, free pages %lld; writing it\n", i, free_pages());
        write_pages(p, step);
    }
    printf("[check] done\n");
    return 0;
}

/* File behaviour on KasperskyOS CE 1.4.0.102 (QEMU, aarch64) that differs from POSIX or Linux.
 * Runs with VfsRamFs as its file system (/tmp is its RAM file system, /dev its devfs). Every check prints one
 * "[check]" line. */
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>

static void result(const char* what, long rc)
{
    int e = errno;
    printf("[check] %-58s -> %ld%s%s\n", what, rc, rc < 0 ? ", errno " : "", rc < 0 ? strerror(e) : "");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    char name[64];
    char io[8] = { 0 };
    struct stat st;

    /* mkstemps with a suffix */
    strcpy(name, "/tmp/tmpXXXXXX.tmp");
    errno = 0;
    result("mkstemps(\"/tmp/tmpXXXXXX.tmp\", 4)", mkstemps(name, 4));
    strcpy(name, "/tmp/tmpXXXXXX");
    errno = 0;
    result("mkstemp(\"/tmp/tmpXXXXXX\")", mkstemp(name));

    /* hard links */
    int fd = open("/tmp/plain.txt", O_CREAT | O_WRONLY | O_TRUNC, 0600);
    close(fd);
    errno = 0;
    result("link(\"/tmp/plain.txt\", \"/tmp/hard.txt\")", link("/tmp/plain.txt", "/tmp/hard.txt"));

    /* /dev/null: write, then the offset, truncate and sync calls */
    fd = open("/dev/null", O_RDWR);
    errno = 0;
    result("write(/dev/null, 8)", (long)write(fd, io, 8));
    errno = 0;
    result("pwrite(/dev/null, 8, offset 0)", (long)pwrite(fd, io, 8, 0));
    errno = 0;
    result("pread(/dev/null, 8, offset 0)", (long)pread(fd, io, 8, 0));
    errno = 0;
    result("ftruncate(/dev/null, 0)", ftruncate(fd, 0));
    errno = 0;
    result("fsync(/dev/null)", fsync(fd));
    close(fd);

    /* pipes: POSIX gives ESPIPE for pread and EINVAL for fsync and (POSIX.1-2024) ftruncate, as Linux does */
    int p[2];
    pipe(p);
    write(p[1], "ab", 2);
    errno = 0;
    result("pread(pipe, 2, offset 0)", (long)pread(p[0], io, 2, 0));
    errno = 0;
    result("fsync(pipe write end)", fsync(p[1]));
    errno = 0;
    result("ftruncate(pipe write end, 0)", ftruncate(p[1], 0));
    close(p[0]);
    close(p[1]);

    /* uid 0 and permissions */
    printf("[check] %-58s -> %d\n", "geteuid()", (int)geteuid());
    fd = open("/tmp/readonly.txt", O_CREAT | O_WRONLY | O_TRUNC, 0600);
    close(fd);
    chmod("/tmp/readonly.txt", 0444);
    errno = 0;
    fd = open("/tmp/readonly.txt", O_WRONLY);
    result("open(0444 file it created, O_WRONLY)", fd);
    if (fd >= 0)
        close(fd);
    struct timespec times[2];
    clock_gettime(CLOCK_REALTIME, &times[0]);
    times[1] = times[0];
    errno = 0;
    result("utimensat(0444 file it created, explicit times)", utimensat(AT_FDCWD, "/tmp/readonly.txt", times, 0));

    /* setuid and setgid bits */
    fd = open("/tmp/modes.txt", O_CREAT | O_WRONLY | O_TRUNC, 0600);
    close(fd);
    chmod("/tmp/modes.txt", 06755);
    stat("/tmp/modes.txt", &st);
    printf("[check] %-58s -> mode %o\n", "chmod(file, 06755)", (unsigned)st.st_mode);
    mkdir("/tmp/modes.dir", 0755);
    chmod("/tmp/modes.dir", 06755);
    stat("/tmp/modes.dir", &st);
    printf("[check] %-58s -> mode %o\n", "chmod(directory, 06755)", (unsigned)st.st_mode);

    /* unlink of a directory (POSIX: EPERM unless privileged and supported; Linux: EISDIR) */
    mkdir("/tmp/empty.dir", 0755);
    errno = 0;
    result("unlink(empty directory)", unlink("/tmp/empty.dir"));
    errno = 0;
    result("  stat(that directory) afterwards", stat("/tmp/empty.dir", &st));

    /* rename to a path longer than PATH_MAX, one component longer than NAME_MAX (POSIX: ENAMETOOLONG) */
    static char longpath[32800];
    memset(longpath, 'a', sizeof(longpath) - 1);
    memcpy(longpath, "/tmp/", 5);
    errno = 0;
    result("rename(file, 32799-character path)", rename("/tmp/plain.txt", longpath));

    /* statvfs: size and free space of /tmp, before and while it holds a 4 MiB file */
    static char chunk[65536];
    struct statvfs vfs;
    errno = 0;
    if (statvfs("/tmp", &vfs) == 0)
        printf("[check] %-58s -> %llu, %llu, %llu (f_frsize %lu)\n", "statvfs(/tmp): f_blocks, f_bfree, f_bavail",
            (unsigned long long)vfs.f_blocks, (unsigned long long)vfs.f_bfree, (unsigned long long)vfs.f_bavail,
            (unsigned long)vfs.f_frsize);
    else
        result("statvfs(/tmp)", -1);
    fd = open("/tmp/4mib.bin", O_CREAT | O_WRONLY | O_TRUNC, 0600);
    for (int i = 0; i < 64; i++)
        write(fd, chunk, sizeof(chunk));
    close(fd);
    errno = 0;
    if (statvfs("/tmp", &vfs) == 0)
        printf("[check] %-58s -> %llu, %llu, %llu\n", "  the same with a 4 MiB file in /tmp",
            (unsigned long long)vfs.f_blocks, (unsigned long long)vfs.f_bfree, (unsigned long long)vfs.f_bavail);
    else
        result("  statvfs(/tmp) with a 4 MiB file", -1);
    unlink("/tmp/4mib.bin");

    /* space allocation beyond what the file system has */
    fd = open("/tmp/big.bin", O_CREAT | O_RDWR | O_TRUNC, 0600);
    errno = 0;
    int pf = posix_fallocate(fd, 0, (off_t)1 << 40);
    printf("[check] %-58s -> %d (errno %d)\n", "posix_fallocate(fd, 0, 1 TiB) return value", pf, errno);
    errno = 0;
    result("ftruncate(fd, 1 TiB)", ftruncate(fd, (off_t)1 << 40));
    close(fd);

    printf("[check] done\n");
    return 0;
}

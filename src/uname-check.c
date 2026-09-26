/* uname() on KasperskyOS CE 1.4.0.102 (QEMU, aarch64). */
#include <stdio.h>
#include <sys/utsname.h>

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    struct utsname u;
    int rc = uname(&u);
    printf("[check] uname() -> %d\n", rc);
    printf("[check] sysname  \"%s\"\n", u.sysname);
    printf("[check] nodename \"%s\"\n", u.nodename);
    printf("[check] release  \"%s\"\n", u.release);
    printf("[check] version  \"%s\"\n", u.version);
    printf("[check] machine  \"%s\"\n", u.machine);
    printf("[check] done\n");
    return 0;
}

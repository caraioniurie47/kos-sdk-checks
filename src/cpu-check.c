/* CPU feature queries on KasperskyOS CE 1.4.0.102 (QEMU, aarch64, -cpu cortex-a57, which has Advanced SIMD). */
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>

static void query(const char* name)
{
    int value = -1;
    size_t len = sizeof(value);
    errno = 0;
    int rc = sysctlbyname(name, &value, &len, NULL, 0);
    printf("[check] sysctlbyname(\"%s\") -> %d%s%s, value %d\n", name, rc, rc ? ", errno " : "",
           rc ? strerror(errno) : "", value);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    query("hw.optional.AdvSIMD");
    query("hw.optional.arm.AdvSIMD");
    query("hw.optional.floatingpoint");
    query("hw.optional.arm.FEAT_AES");
    query("hw.ncpu");
    printf("[check] done\n");
    return 0;
}

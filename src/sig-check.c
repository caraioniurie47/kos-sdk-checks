/* Signal handler installation on KasperskyOS CE 1.4.0.102: sigaction for signals POSIX says cannot be caught (SIGKILL,
 * SIGSTOP), with SIGTERM and SIGUSR1 as controls. Each installs a handler, then reads the action back. */
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

static void handler(int sig)
{
    (void)sig;
}

static void install(const char* name, int sig)
{
    struct sigaction sa, back;
    memset(&sa, 0, sizeof(sa));
    memset(&back, 0, sizeof(back));
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    errno = 0;
    int rc = sigaction(sig, &sa, NULL);
    int e = errno;
    sigaction(sig, NULL, &back);
    printf("[check] sigaction(%s, handler) -> %d%s%s; handler read back: %s\n", name, rc, rc ? ", errno " : "",
           rc ? strerror(e) : "", back.sa_handler == handler ? "installed" : "not installed");
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    install("SIGKILL", SIGKILL);
    install("SIGSTOP", SIGSTOP);
    install("SIGTERM", SIGTERM);
    install("SIGUSR1", SIGUSR1);
    printf("[check] done\n");
    return 0;
}

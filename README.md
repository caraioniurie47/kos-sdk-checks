# KasperskyOS SDK checks

Small programs and scripts that show where KasperskyOS Community Edition 1.4.0.102 (QEMU, aarch64) behaves differently
from POSIX or Linux, or where the SDK lacks something portable code expects. I found these while porting .NET
(NativeAOT) to KasperskyOS ([runtime-kos](https://github.com/caraioniurie47/runtime-kos)). **[`FINDINGS.md`](FINDINGS.md)
describes each finding**: what the checks show, what POSIX, NetBSD and Linux do, what it breaks, and the options; the
last column below links the findings each check backs.

| Check | What it shows | Findings |
|---|---|---|
| [`src/net-check.c`](src/net-check.c) | IPv6, `localhost`, `sendfile`, `shutdown` of TCP and UDP sockets, `accept4` and `O_NONBLOCK`, `poll` with VfsNet; also `recv` of more than 64 KiB, which did not fail | [B5](FINDINGS.md#b5), [B6](FINDINGS.md#b6), [B7](FINDINGS.md#b7), [E2](FINDINGS.md#e2), [E3](FINDINGS.md#e3), [U4](FINDINGS.md#u4), [Portability notes](FINDINGS.md#portability-notes) |
| [`src/net2-check.c`](src/net2-check.c) | `sendmsg` of more than 64 KiB on TCP, non-blocking and blocking, `setsockopt` after the peer's reset | [U8](FINDINGS.md#u8), [U10](FINDINGS.md#u10) |
| [`src/net3-check.c`](src/net3-check.c) | UDP `connect(AF_UNSPEC)`, zero-length `recv`, `FIONREAD`, blocked calls woken by `close`, `SO_SNDBUF`/`SO_RCVBUF` 0, `SO_REUSEPORT`, `getsockopt` with a NULL buffer, `IOV_MAX`, `sendto` and `sendmsg` of zero bytes, `accept4` address length, `connect` and `accept` with no descriptor left | [B8](FINDINGS.md#b8), [B9](FINDINGS.md#b9), [B10](FINDINGS.md#b10), [B11](FINDINGS.md#b11), [B12](FINDINGS.md#b12), [U5](FINDINGS.md#u5), [U6](FINDINGS.md#u6), [U7](FINDINGS.md#u7), [U9](FINDINGS.md#u9), [U11](FINDINGS.md#u11), [M1](FINDINGS.md#m1) |
| [`src/sys-check.c`](src/sys-check.c) | `pthread_condattr_init` on an initialized attribute, `sysconf`, `readdir` of a removed directory, `mprotect` of a read-only image page, `getrlimit`/`setrlimit` and the descriptor limit | [B13](FINDINGS.md#b13), [U3](FINDINGS.md#u3), [U13](FINDINGS.md#u13), [U17](FINDINGS.md#u17), [M2](FINDINGS.md#m2) |
| [`src/oom-check.c`](src/oom-check.c) | running out of memory with plain and with `MAP_NORESERVE` mappings | [U16](FINDINGS.md#u16) |
| [`src/fs-check.c`](src/fs-check.c) | `mkstemps`, `link`, `/dev/null` and pipes, uid 0 and mode bits, `unlink`, `rename`, `posix_fallocate`, `utimensat`, `statvfs` on VfsRamFs | [B1](FINDINGS.md#b1), [B2](FINDINGS.md#b2), [B3](FINDINGS.md#b3), [E1](FINDINGS.md#e1), [U1](FINDINGS.md#u1), [U2](FINDINGS.md#u2) |
| [`src/uname-check.c`](src/uname-check.c) | what `uname()` returns | [U12](FINDINGS.md#u12) |
| [`src/novfs-check.c`](src/novfs-check.c) | a program linked with the VFS client in an image without a VFS program: the wait at startup | [U14](FINDINGS.md#u14) |
| [`host/sdk-check.sh`](host/sdk-check.sh) | OpenSSL headers, `getentropy`, GSSAPI, functions declared but defined nowhere, `FALLOC_FL_*`, commonly probed headers, `dlerror`'s declaration, `SA_RESETHAND`/`SA_NODEFER`, `<uchar.h>`, `SO_REUSEPORT`, `OPEN_MAX`/`NAME_MAX`/`IOV_MAX`, `sendfile`, IPv6 in the network stack's package, `getauxval`, the compiler's C dialect | [B3](FINDINGS.md#b3), [B11](FINDINGS.md#b11), [B13](FINDINGS.md#b13), [B16](FINDINGS.md#b16), [E2](FINDINGS.md#e2), [E3](FINDINGS.md#e3), [M1](FINDINGS.md#m1), [M3](FINDINGS.md#m3), [M4](FINDINGS.md#m4), [M5](FINDINGS.md#m5), [M6](FINDINGS.md#m6), [M7](FINDINGS.md#m7), [M8](FINDINGS.md#m8), [M9](FINDINGS.md#m9), [M10](FINDINGS.md#m10), [P3](FINDINGS.md#p3), [Portability notes](FINDINGS.md#portability-notes) |
| [`src/mem-check.c`](src/mem-check.c) | `mmap(PROT_NONE)` reservations, `MADV_DONTNEED`, `MADV_FREE`, write-and-execute mappings | [B15](FINDINGS.md#b15), [U15](FINDINGS.md#u15) |
| [`src/cpu-check.c`](src/cpu-check.c) | CPU feature queries through `sysctlbyname` | [P3](FINDINGS.md#p3) |
| [`src/sig-check.c`](src/sig-check.c) | `sigaction` for `SIGKILL` and `SIGSTOP` | [B14](FINDINGS.md#b14) |
| [`host/thread-api-check.sh`](host/thread-api-check.sh) | thread suspension and register access in the headers; what the exception handler API documents | [P1](FINDINGS.md#p1), [P2](FINDINGS.md#p2) |
| [`host/toolchain-check.sh`](host/toolchain-check.sh) | `-static-pie`, unprefixed compilers, typos in log messages; also where the manual says that stdout needs a VFS program (documented, p. 94: nothing to report) | [E4](FINDINGS.md#e4), [U18](FINDINGS.md#u18), [U19](FINDINGS.md#u19) |

The output of each, as run on 2026-09-24 (`mem` and `sig` on 2026-09-25; `oom` on 2026-09-29; `fs` on 2026-09-30;
`novfs`, `net`, `net2`, `net3`, `sys`, `sdk-check.sh` and `thread-api-check.sh` on 2026-10-02), is in
[`results/`](results/).

The VfsRamFs crash ([B4](FINDINGS.md#b4)) has its own reproducer:
[kos-vfsramfs-repro](https://github.com/caraioniurie47/kos-vfsramfs-repro).

## Programs on KasperskyOS

Each program in `src/` is the only application in its image, as `checks.Check`. Every image but `novfs-check.c`'s has
the SDK's prebuilt `VfsRamFs` as the program's file system (`/tmp` is a RAM file system, `/dev` its devfs) and `VfsNet`
as its network stack; `novfs-check.c`'s has no VFS program. The security policy grants everything. `net-check.c`,
`net2-check.c` and `net3-check.c` give `en0` the address QEMU user networking expects, as the SDK's network examples
do. Every check prints one `[check]` line and the program ends with `[check] done`, except `oom-check.c`, which the
kernel is expected to end. `novfs-check.c` prints to stderr, as stdout needs a VFS program; for it,
`host/run-check.sh` also prints the C runtime's VFS log lines, whose timestamps show the wait.

With the SDK's CMake (`SDK` is the SDK's install directory, `CHECK` one of `net`, `net2`, `net3`, `fs`, `mem`, `sys`,
`oom`, `uname`, `cpu`, `sig`, `novfs`):

```
$SDK/toolchain/bin/cmake -B build-net -D CMAKE_TOOLCHAIN_FILE=$SDK/toolchain/share/toolchain-aarch64-kos.cmake -D CHECK=net
$SDK/toolchain/bin/cmake --build build-net --target sim
```

or `host/run-check.sh net [SDK directory]`, which does both, prints the `[check]` lines and stops QEMU after
`[check] done`. The SDK's CMake warns "Do not use build_kos_xxx_image() in root 'CMakeLists.txt'"; the image builds
and runs regardless.

## The same programs on Linux

`linux/run-linux.sh net`, `net2`, `net3`, `fs`, `sys` or `sig`, as root, compiles the program with `gcc` and runs it with a
private 64 MiB tmpfs as `/tmp`. `linux/kos_net.h` replaces the SDK's network setup helpers with stubs. The other
programs use KasperskyOS interfaces and have no Linux counterpart.

## Scripts on the host

`host/sdk-check.sh`, `host/thread-api-check.sh` and `host/toolchain-check.sh` inspect an installed SDK (default
`/opt/KasperskyOS-Community-Edition-Qemu-1.4.0.102`, or the directory given as the first argument). Each prints every
command as `$ <command>` before its output.

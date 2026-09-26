# KasperskyOS SDK checks

Small programs and scripts that show where KasperskyOS Community Edition 1.4.0.102 (QEMU, aarch64) behaves differently
from POSIX or Linux, or where the SDK lacks something portable code expects. I found these while porting .NET
(NativeAOT) to KasperskyOS ([runtime-kos](https://github.com/caraioniurie47/runtime-kos)), and each one backs a report
on the [KasperskyOS forum](https://forum.kaspersky.com/forum/kasperskyos-development-268/).

| Check | What it shows | Forum report |
|---|---|---|
| [`src/net-check.c`](src/net-check.c) | IPv6, `localhost`, `sendfile`, `shutdown`, `accept4` and `O_NONBLOCK`, `poll` with VfsNet; also `recv` of more than 64 KiB, which did not fail | Sockets |
| [`src/fs-check.c`](src/fs-check.c) | `mkstemps`, `link`, `/dev/null` and pipes, uid 0 and mode bits, `unlink`, `rename`, `posix_fallocate`, `utimensat`, `statvfs` on VfsRamFs | Files |
| [`src/uname-check.c`](src/uname-check.c) | what `uname()` returns | Sysroot |
| [`host/sdk-check.sh`](host/sdk-check.sh) | OpenSSL headers, `getentropy`, functions declared but defined nowhere, `FALLOC_FL_*`, commonly probed headers | Sysroot |
| [`src/mem-check.c`](src/mem-check.c) | `mmap(PROT_NONE)` reservations, `MADV_DONTNEED`, `MADV_FREE`, write-and-execute mappings | Memory |
| [`src/cpu-check.c`](src/cpu-check.c) | CPU feature queries through `sysctlbyname` | Managed runtime |
| [`src/sig-check.c`](src/sig-check.c) | `sigaction` for `SIGKILL` and `SIGSTOP` | Managed runtime |
| [`host/thread-api-check.sh`](host/thread-api-check.sh) | thread suspension and register access in the headers | Managed runtime |
| [`host/toolchain-check.sh`](host/toolchain-check.sh) | `-static-pie`, unprefixed compilers, where the SDK says that stdout needs a VFS program, typos in log messages | Toolchain |

The output of each, as run on 2026-09-24 (`mem` and `sig` on 2026-09-25), is in [`results/`](results/).

## What each behaviour is

**defect**: differs from POSIX or breaks portable code; **docs wrong**: the manual states something the check
contradicts; **gap**: a library, header or API is missing; **docs**: allowed behaviour that the documentation does not
mention. "Manual" is the KasperskyOS Community Edition 1.4 manual (PDF); "POSIX" is IEEE Std 1003.1-2017.

| Behaviour | Check | Status |
|---|---|---|
| `socket(AF_INET6, ...)` fails `EAFNOSUPPORT` | net | docs wrong: the manual documents IPv6 configuration for `kos_net.h` ("Configure the available network interfaces with IPv4 and IPv6 addressing.", `configure_net_iface6()`, pp. 124-125); the SDK has no IPv6 build of its network stack |
| `localhost` does not resolve without a hosts file for VfsNet | net | docs |
| `sendfile()` from a file to a TCP socket fails `EINVAL` | net | defect; docs wrong: the manual's table "Functions implemented by the vfs::lib_fs library" (pp. 96-97) lists `sendfile()` |
| `shutdown()` of an unconnected TCP socket succeeds (POSIX: `ENOTCONN`) | net | defect |
| a socket accepted from a non-blocking listener is non-blocking | net | docs (as on BSD; POSIX leaves it open) |
| `poll()` fails with `EBADF` for a closed descriptor among open ones (POSIX: `POLLNVAL` in that entry) | net | defect |
| `poll()` fails with `EINVAL` for more than 512 entries, even all -1 | net | allowed: POSIX lists `EINVAL` for more than `{OPEN_MAX}` entries, and the SDK's `limits.h` defines `OPEN_MAX` 512 |
| `mkstemps()` fails `EINVAL` for a valid template | fs | defect |
| `link()` fails `ENOSYS` | fs | docs wrong: the same lib_fs table lists `link()` |
| `pread`, `pwrite`, `ftruncate`, `fsync` fail `ENOSYS` on `/dev/null` and pipes | fs | defect; docs wrong: the lib_fs table lists all four |
| uid 0 without superuser rights; a directory loses the setuid and setgid bits | fs | docs |
| `unlink()` removes an empty directory (POSIX: `EPERM` without appropriate privileges), `rename()` to an over-long path gives `EINVAL` (POSIX: `ENAMETOOLONG`), `posix_fallocate()` returns -1 and sets `errno` (POSIX: returns the error number), `utimensat()` with explicit times fails `EACCES` for the owner | fs | defect |
| VfsRamFs's `statvfs()` reports the space in use as the size and no free space | fs | docs: POSIX says "It is unspecified whether all members of the statvfs structure have meaningful values on all file systems." |
| `uname()` returns constants | uname | defect |
| OpenSSL libraries without headers; no GSSAPI; commonly probed headers absent | sdk-check | gap |
| `libcrypto.a` references `getentropy()`, defined and declared nowhere; functions declared but defined in no library; `fallocate()` without `FALLOC_FL_*` | sdk-check | defect |
| `mmap(PROT_NONE)` takes physical memory; `MADV_DONTNEED` and `MADV_FREE` free nothing | mem | defect |
| `mprotect()` to read, write and execute fails with `ENOMEM` | mem | docs wrong: refusing write-and-execute is documented, with `ENOTSUP`, in the POSIX limitations' `mprotect()` row (p. 452) |
| `sigaction()` installs a handler for `SIGKILL` and `SIGSTOP` (POSIX: `EINVAL`) | sig | defect |
| no CPU feature query: none of the `hw.optional` names tried exists for `sysctlbyname`, and there is no `sys/auxv.h` (sdk-check) | cpu | gap |
| no API to suspend another running thread (the headers have `KnThreadSuspendCurrent` only; `KnTaskGetThreadContext` reads a thread of a frozen process) | thread-api-check | gap |
| `aarch64-kos-clang -static-pie` links dynamically | toolchain-check | defect |
| unprefixed `clang`/`clang-17` target KasperskyOS | toolchain-check | docs |
| stdout needs a VFS program | toolchain-check | documented: the manual says so (p. 94), besides the hello example's comment; nothing to report |
| "connetion", "succesfully" in log messages | toolchain-check | typo |

## Programs on KasperskyOS

Each program in `src/` is the only application in its image, as `checks.Check`, with the SDK's prebuilt `VfsRamFs` as
its file system (`/tmp` is a RAM file system, `/dev` its devfs) and `VfsNet` as its network stack. The security
policy grants everything. `net-check.c` gives `en0` the address QEMU user networking expects, as the SDK's network
examples do. Every check prints one `[check]` line and the program ends with `[check] done`.

With the SDK's CMake (`SDK` is the SDK's install directory, `CHECK` one of `net`, `fs`, `mem`, `uname`, `cpu`, `sig`):

```
$SDK/toolchain/bin/cmake -B build-net -D CMAKE_TOOLCHAIN_FILE=$SDK/toolchain/share/toolchain-aarch64-kos.cmake -D CHECK=net
$SDK/toolchain/bin/cmake --build build-net --target sim
```

or `host/run-check.sh net [SDK directory]`, which does both, prints the `[check]` lines and stops QEMU after
`[check] done`. The SDK's CMake warns "Do not use build_kos_xxx_image() in root 'CMakeLists.txt'"; the image builds
and runs regardless.

## The same programs on Linux

`linux/run-linux.sh net`, `fs` or `sig`, as root, compiles the program with `gcc` and runs it with a
private 64 MiB tmpfs as `/tmp`. `linux/kos_net.h` replaces the SDK's network setup helpers with stubs. The other
programs use KasperskyOS interfaces and have no Linux counterpart.

## Scripts on the host

`host/sdk-check.sh`, `host/thread-api-check.sh` and `host/toolchain-check.sh` inspect an installed SDK (default
`/opt/KasperskyOS-Community-Edition-Qemu-1.4.0.102`, or the directory given as the first argument). Each prints every
command as `$ <command>` before its output.

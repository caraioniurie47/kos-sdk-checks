# KasperskyOS SDK checks

Small programs and scripts that show where KasperskyOS Community Edition 1.4.0.102 (QEMU, aarch64) behaves differently
from POSIX or Linux, or where the SDK lacks something portable code expects. I found these while porting .NET
(NativeAOT) to KasperskyOS ([runtime-kos](https://github.com/caraioniurie47/runtime-kos)), and each one backs a report
on the [KasperskyOS forum](https://forum.kaspersky.com/forum/kasperskyos-development-268/).

| Check | What it shows | Forum report |
|---|---|---|
| [`src/net-check.c`](src/net-check.c) | IPv6, `localhost`, `sendfile`, `shutdown`, `accept4` and `O_NONBLOCK`, `poll` with VfsNet; also `recv` of more than 64 KiB, which did not fail | Sockets |
| [`src/net2-check.c`](src/net2-check.c) | `sendmsg` of more than 64 KiB on TCP, `setsockopt` after the peer's reset | Sockets (2) |
| [`src/net3-check.c`](src/net3-check.c) | UDP `connect(AF_UNSPEC)`, zero-length `recv`, `FIONREAD`, a blocked call woken by `close`, `SO_SNDBUF`/`SO_RCVBUF` 0, `SO_REUSEPORT`, `getsockopt` with a NULL buffer, `IOV_MAX`, `sendto` of zero bytes, `accept4` address length | Sockets (2) |
| [`src/sys-check.c`](src/sys-check.c) | `pthread_condattr_init` on an initialized attribute, `sysconf`, `readdir` of a removed directory, `mprotect` of a read-only image page | Sysroot, Files, Memory |
| [`src/oom-check.c`](src/oom-check.c) | running out of memory with plain and with `MAP_NORESERVE` mappings | Memory |
| [`src/fs-check.c`](src/fs-check.c) | `mkstemps`, `link`, `/dev/null` and pipes, uid 0 and mode bits, `unlink`, `rename`, `posix_fallocate`, `utimensat`, `statvfs` on VfsRamFs | Files |
| [`src/uname-check.c`](src/uname-check.c) | what `uname()` returns | Sysroot |
| [`host/sdk-check.sh`](host/sdk-check.sh) | OpenSSL headers, `getentropy`, functions declared but defined nowhere, `FALLOC_FL_*`, commonly probed headers, `dlerror`'s declaration, `SA_RESETHAND`/`SA_NODEFER`, `<uchar.h>`, `SO_REUSEPORT` | Sysroot |
| [`src/mem-check.c`](src/mem-check.c) | `mmap(PROT_NONE)` reservations, `MADV_DONTNEED`, `MADV_FREE`, write-and-execute mappings | Memory |
| [`src/cpu-check.c`](src/cpu-check.c) | CPU feature queries through `sysctlbyname` | Managed runtime |
| [`src/sig-check.c`](src/sig-check.c) | `sigaction` for `SIGKILL` and `SIGSTOP` | Managed runtime |
| [`host/thread-api-check.sh`](host/thread-api-check.sh) | thread suspension and register access in the headers | Managed runtime |
| [`host/toolchain-check.sh`](host/toolchain-check.sh) | `-static-pie`, unprefixed compilers, where the SDK says that stdout needs a VFS program, typos in log messages | Toolchain |

The output of each, as run on 2026-09-24 (`mem` and `sig` on 2026-09-25; `net2` before its commit on 2026-09-26;
`net3`, `sys`, `oom` and the last four sections of `sdk-check.sh` on 2026-09-29), is in [`results/`](results/).

## What each behaviour is

**defect**: differs from POSIX or breaks portable code; **docs wrong**: the manual states something the check
contradicts; **gap**: a library, header or API is missing; **docs**: allowed behaviour that the documentation does not
mention; **allowed**: behaviour POSIX requires, listed because Linux differs. "Manual" is the KasperskyOS Community
Edition 1.4 manual (PDF); "POSIX" is IEEE Std 1003.1-2017, unless a row names POSIX.1-2024.

| Behaviour | Check | Status |
|---|---|---|
| `socket(AF_INET6, ...)` fails `EAFNOSUPPORT` | net | docs wrong: the manual documents IPv6 configuration for `kos_net.h` ("Configure the available network interfaces with IPv4 and IPv6 addressing.", `configure_net_iface6()`, pp. 124-125); this SDK package's network stack has no IPv6 library (`rumpConfig.cmake` adds `rump::rumpnet_netinet6` only if that target exists, and there is no `librumpnet_netinet6.a`) |
| `localhost` does not resolve without a hosts file for VfsNet | net | docs |
| `sendfile()` from a file to a TCP socket fails `EINVAL` | net | defect; docs wrong: the manual's table "Functions implemented by the vfs::lib_fs library" (pp. 96-97) lists `sendfile()` |
| `shutdown()` of an unconnected TCP socket succeeds (POSIX: `ENOTCONN`) | net | defect (as in NetBSD 10, whose `soshutdown()` has no connection check) |
| a socket from `accept4()` without `SOCK_NONBLOCK`, from a non-blocking listener, is non-blocking | net | defect: POSIX.1-2024 says `accept4()` takes `O_NONBLOCK` "solely" from its flags, and NetBSD's `accept4()` (`paccept()`) clears it; only `accept()` may inherit it |
| `poll()` fails with `EBADF` for a closed descriptor among open ones (POSIX: `POLLNVAL` in that entry) | net | defect |
| `poll()` fails with `EINVAL` for more than 512 entries, even all -1 | net | allowed: POSIX requires `EINVAL` for more than `{OPEN_MAX}` entries, and the SDK's `limits.h` defines `OPEN_MAX` 512 |
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
| a connected UDP socket: `connect(AF_UNSPEC)` fails `EAFNOSUPPORT` (POSIX: "the socket's peer address shall be reset"), and `connect(0.0.0.0:0)` fails | net3 | defect |
| `recv()` of zero bytes on an empty non-blocking TCP socket returns 0 (POSIX: `EAGAIN`) | net3 | defect (as NetBSD's `soreceive`) |
| `FIONREAD` on UDP counts 16 bytes more than the datagram | net3 | docs (NetBSD counts the sender's address too) |
| a `read`/`recvmsg` blocked while another thread closes the socket fails with `errno` -3, which is no error number | net3 | defect |
| `SO_SNDBUF`/`SO_RCVBUF` of 0 fail `EINVAL` | net3 | docs |
| the stack honours `SO_REUSEPORT` (0x0200, `RUMP_SO_REUSEPORT` in `rump/rumpdefs.h`), `sys/socket.h` does not define it | net3, sdk-check | gap |
| `getsockopt()` with a NULL buffer and length 0 fails `EINVAL` | net3 | docs (NetBSD succeeds) |
| `IOV_MAX` is 10; `sendmsg()` with more iovecs fails `EINVAL` (POSIX: `EMSGSIZE`) | net3 | defect; the limit itself is documented (`MaxIovecsCount`, p. 869) |
| `sendto()` of zero bytes on UDP returns 0 and sends nothing | net3 | defect |
| `accept4()` with `address_len` over 128 fails `EACCES` (POSIX: the address is truncated) | net3 | docs (the IPC's `MaxSockAddrSize`, p. 869, is 128) |
| `sendmsg()` of more than 64 KiB on TCP fails `EMSGSIZE`, while `send()` sends part | net2 | docs |
| `setsockopt()` fails `ECONNRESET` after the peer's reset | net2 | docs |
| `pthread_condattr_init()` fails `EINVAL` on an initialized attribute or a byte copy of one | sys | docs (POSIX: undefined) |
| `sysconf(_SC_PHYS_PAGES)` fails `EINVAL` though `unistd.h` defines the name | sys | gap |
| `readdir()` of a directory removed after `opendir()` fails `ENOENT` (Linux: end of stream) | sys | docs (POSIX lists `ENOENT`) |
| `mprotect()` cannot raise a read-only image page to writable (`EACCES`); lowering works | sys | docs (POSIX: unspecified for memory not from `mmap()`) |
| `MAP_NORESERVE` memory is committed on first write, and when none is left the kernel ends the process (`Unhandled Overcommit`); a plain `mmap()` fails `ENOMEM` instead | oom | docs |
| `dlerror()` is declared `const char *` (POSIX: `char *`) | sdk-check | defect |
| no `SA_RESETHAND`, `SA_NODEFER` (POSIX base since Issue 7) | sdk-check | gap |
| no C `<uchar.h>` or its four functions (C11, POSIX.1-2024) | sdk-check | gap |

## Programs on KasperskyOS

Each program in `src/` is the only application in its image, as `checks.Check`, with the SDK's prebuilt `VfsRamFs` as
its file system (`/tmp` is a RAM file system, `/dev` its devfs) and `VfsNet` as its network stack. The security
policy grants everything. `net-check.c` gives `en0` the address QEMU user networking expects, as the SDK's network
examples do. Every check prints one `[check]` line and the program ends with `[check] done`, except `oom-check.c`,
which the kernel is expected to end.

With the SDK's CMake (`SDK` is the SDK's install directory, `CHECK` one of `net`, `net2`, `net3`, `fs`, `mem`, `sys`,
`oom`, `uname`, `cpu`, `sig`):

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

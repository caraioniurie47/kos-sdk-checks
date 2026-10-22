# KasperskyOS CE 1.4.0.102 and 2.0.0.179: findings

Where KasperskyOS Community Edition 1.4.0.102 (`KasperskyOS-Community-Edition-Qemu`, QEMU, aarch64) behaves
differently from POSIX or Linux, or lacks something portable code expects, and what 2.0.0.179
(`KasperskyOS-Community-Edition-Qemu-arm64`) does in each case. I found these while porting .NET (NativeAOT)
to KasperskyOS (latest release: https://github.com/caraioniurie47/runtime-kos/releases/latest). Each section names the
check that shows it, in this repository with its output in [`results/`](results/) (1.4) and
[`results/2.0.0.179/`](results/2.0.0.179/) (2.0), or for [B4](#b4) the reproducer in its own repository.

**How the checks ran.** Each program in `src/` ran as the only application in an image with the SDK's
`precompiled_vfs::VfsNet` as its network backend and `precompiled_vfs::VfsRamFs` as its file system (`/tmp` is its RAM
file system, `/dev` its devfs); the security policy grants everything. The network programs (`net`, `net2`, `net3`)
first configure `en0` as the SDK's network examples do. `novfs` is the exception: its image has no VFS program (1.4). The
`net`, `net2`, `net3`, `fs`, `sys` and `sig` programs also ran on Linux for comparison (Ubuntu 22.04, kernel 6.18 under
WSL2, as root, `/tmp` a 64 MiB tmpfs), with the two `kos_net.h` calls replaced by stubs. `host/sdk-check.sh`,
`host/thread-api-check.sh` and `host/toolchain-check.sh` inspect an installed SDK (`$SDK`) and print each command before
its output; commands with relative paths run in `$SDK/sysroot-aarch64-kos` (`thread-api-check.sh`'s in its `include/`,
`toolchain-check.sh`'s in a temporary directory).
How to build and run each check: [`README.md`](README.md).

**On 2.0.0.179** the same programs ran, built by [`sdk2/`](sdk2/): SDK 2.0 builds no image around Einit alone
([U21](#u21)), so each program is the KPA package `Check` on the SDK's prebuilt MinimalKOS, started at boot by ExecMgr.
VfsRamFs, VfsNet and the `dnet` network programs run from a product ROMFS with the environment and arguments the SDK
gives them by default, as in the SDK's `net_with_separate_vfs` example; VfsNet runs on the `dnet` driver. `novfs` has
no product ROMFS. The security
policy grants everything on top of MinimalKOS's own. QEMU runs with `-cpu cortex-a55` (1.4: `-cpu cortex-a57`) and
`-m 2048`. The Linux outputs are those of the 1.4 runs.

**Categories.** "POSIX" is POSIX.1-2024 unless a section names another edition; "the manual" is the KasperskyOS
Community Edition 1.4 manual and "the 2.0 manual" that of 2.0 (PDFs; page numbers as printed). Each citation also
links the page of the online documentation that holds the passage, for a quick check (checked 2026-10-22).

| Category | What it means |
|---|---|
| Bugs | Contradicts a "shall" of POSIX, or, for a function POSIX does not have, the documentation of NetBSD, which the SDK's libc and network stack come from; or a system program crashes. An `errno` counts when POSIX describes the condition: implementations "shall not generate a different error number from one required by this volume of POSIX.1-2024 for an error condition described in this volume of POSIX.1-2024" ([XSH 2.3](https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html#tag_16_03)). |
| Documentation errors | The manual states something a check contradicts; typos. |
| Undocumented behaviour | Behaviour POSIX allows or leaves open, or of a function POSIX does not have, that differs from Linux, or behaviour of the SDK's tools and libraries that portable code does not expect; in each case, that the documentation does not mention. |
| Missing features | A library, header, function or constant that portable code expects and the SDK lacks. |
| Proposals | An interface KasperskyOS has no equivalent for, with what it would let software do. |

Most sections end with options: what POSIX, NetBSD or Linux do, keeping the current behaviour and documenting it, or
another choice of yours.

## Status on 2.0.0.179

Every finding was re-checked on 2.0.0.179: the programs and host scripts ran again (2026-10-19 to 2026-10-22), and the
2.0 manual was searched for each point about the documentation. Each section says what changed.

- Changed or new in 2.0: [B3](#b3) (`posix_fallocate()` returns 0 for more space than the file system has, after
  minutes), [U20](#u20) (a `close()` during another thread's `poll()` ends the process), [U21](#u21)
  (`build_kos_qemu_image()` no longer builds an image, and the breaking changes do not say so).
- Partly fixed: [B2](#b2) (`/dev/null` reads and writes, `pread` on a pipe), [E4](#e4) (one typo gone), [M3](#m3)
  (OpenSSL 3.5.7, still without headers).
- Fixed: [B4](#b4). Documented, with options: [U16](#u16). Not testable on 2.0: [U14](#u14).
- Unchanged: every other finding; [U8](#u8) and [B12](#b12) show other byte and write counts.

## Bugs

<a id="b1"></a>
### B1. `mkstemps()` fails with `EINVAL` for a valid template and suffix, while `mkstemp()` works

| Check (`fs-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `mkstemps("/tmp/tmpXXXXXX.tmp", 4)` | -1, errno Invalid argument | -1, errno Invalid argument | 3 |
| `mkstemp("/tmp/tmpXXXXXX")` | 3 | 3 | 4 |

`mkstemps()` is not POSIX; [NetBSD's man page](https://man.netbsd.org/NetBSD-10.0/mktemp.3) (the libc comes from NetBSD) gives the template form
`/tmp/tmpXXXXXXsuffix`, which this call follows. .NET's `Path.GetTempFileName()` uses it; on KasperskyOS it failed with
"Invalid argument" until my port created the file itself.

Check: [`src/fs-check.c`](src/fs-check.c); output [`results/fs.kos.out`](results/fs.kos.out), [`results/2.0.0.179/fs.kos.out`](results/2.0.0.179/fs.kos.out),
[`results/fs.linux.out`](results/fs.linux.out).

Options: accept the template form, as NetBSD describes; keep the current behaviour and document it; or another choice
of yours.

<a id="b2"></a>
### B2. On `/dev/null`, `pread`, `pwrite`, `ftruncate` and `fsync` fail with `ENOSYS`, and on a pipe `pread`, `fsync` and `ftruncate` (1.4; partly fixed in 2.0)

| Check (`fs-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `write(/dev/null, 8)` | 8 | 8 | 8 |
| `pwrite(/dev/null, 8, offset 0)` | -1, errno Function not implemented | 8 | 8 |
| `pread(/dev/null, 8, offset 0)` | -1, errno Function not implemented | 0 | 0 |
| `ftruncate(/dev/null, 0)` | -1, errno Function not implemented | -1, errno Function not implemented | -1, errno Invalid argument |
| `fsync(/dev/null)` | -1, errno Function not implemented | -1, errno Function not implemented | -1, errno Invalid argument |
| `pread(pipe, 2, offset 0)` | -1, errno Function not implemented | -1, errno Illegal seek | -1, errno Illegal seek |
| `fsync(pipe write end)` | -1, errno Function not implemented | -1, errno Function not implemented | -1, errno Invalid argument |
| `ftruncate(pipe write end, 0)` | -1, errno Function not implemented | -1, errno Function not implemented | -1, errno Invalid argument |

On a pipe, POSIX.1-2024 gives `ESPIPE` for [`pread`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pread.html) and `EINVAL` for [`fsync`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/fsync.html) and [`ftruncate`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/ftruncate.html), in its "shall fail" lists,
and Linux returns those; on `/dev/null`, Linux lets `pread` and `pwrite` succeed. Portable code handles those; `ENOSYS`
it does not. The manual's table "Functions implemented by the vfs::lib_fs library" (pp. 96-97 of the 1.4 PDF; [web page](https://support.kaspersky.com/kos-community-edition/2.0/vfs_overview)) lists all
four. (Correction, 2026-10-22: another table of the same manual, "Description and implementation details of the
functions of the standard C library for file system management", says of `fsync()` and `fdatasync()`: "It works if the
call is supported in the file system in which the file is open, otherwise it returns the ENOSYS error." (p. 116 of
the 1.4 PDF, p. 119 of the 2.0 PDF; [web page](https://support.kaspersky.com/kos-community-edition/2.0/vfs_filesys_specialities)). So `ENOSYS` from `fsync` is documented; on a pipe POSIX's "shall fail" `EINVAL`
still applies (XSH 2.3, above).)

**On 2.0.0.179, partly fixed:** `pwrite` and `pread` on `/dev/null` succeed, as on Linux, and `pread` on a pipe fails
with `ESPIPE`, as POSIX requires; `ftruncate` and `fsync` on `/dev/null` and on a pipe still fail with `ENOSYS`. The
2.0 manual has both tables (p. 103, p. 119; [web page](https://support.kaspersky.com/kos-community-edition/2.0/vfs_overview), [web page](https://support.kaspersky.com/kos-community-edition/2.0/vfs_filesys_specialities)).

.NET falls back from `pread`/`pwrite` to `read`/`write` on `ESPIPE`, and ignores `EINVAL` from `ftruncate` and `fsync`,
as portable code does. With `ENOSYS`, `File.OpenNullHandle()`, writing to `/dev/null` and flushing a pipe threw
"Function not implemented". My port now maps `ENOSYS` on non-regular files to those errors.

Check: [`src/fs-check.c`](src/fs-check.c); output [`results/fs.kos.out`](results/fs.kos.out), [`results/2.0.0.179/fs.kos.out`](results/2.0.0.179/fs.kos.out),
[`results/fs.linux.out`](results/fs.linux.out).

Options: on a pipe, fail as POSIX.1-2024 describes (`ESPIPE`, `EINVAL`, `EINVAL`; 2.0 does the first), and on
`/dev/null` behave as Linux does (as in the table; 2.0 does for `pwrite` and `pread`); keep the current behaviour and
document it; or another choice of yours.

<a id="b3"></a>
### B3. VfsRamFs: `utimensat()`, `rename()` and `posix_fallocate()` differ from POSIX

**`utimensat()` with explicit times fails with `EACCES` on a read-only file the process created.**

| Check (`fs-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `utimensat(0444 file it created, explicit times)` | -1, errno Permission denied | -1, errno Permission denied | 0 |

[POSIX](https://pubs.opengroup.org/onlinepubs/9799919799/functions/utimensat.html): "Only a process with the effective user ID equal to the user ID of the file or with appropriate privileges may
use futimens() or utimensat() with a non-null times argument ...", and its ERRORS say the call "shall fail" with
`[EPERM]` when explicit times are given, "the calling process' effective user ID does not match the owner of the file,
and the calling process does not have appropriate privileges." In .NET, setting a read-only file's times throws
`UnauthorizedAccessException`.

**`rename()` to a path with a component longer than `NAME_MAX` fails with `EINVAL`.**

| Check (`fs-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `rename(file, 32799-character path)` | -1, errno Invalid argument | -1, errno Invalid argument | -1, errno File name too long |

The path is `/tmp/` and one 32794-character component; the SDK's `NAME_MAX` is 511. [POSIX's](https://pubs.opengroup.org/onlinepubs/9799919799/functions/rename.html) "shall fail" list gives
`ENAMETOOLONG` for that ("The length of a component of a pathname is longer than {NAME_MAX}."), as Linux returns. In
.NET, moves to over-long paths report a generic I/O error rather than "path too long".

**`posix_fallocate()` returns -1 instead of an error number.**

| Check (`fs-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `posix_fallocate(fd, 0, 1 TiB) return value` | -1 (errno 12) | not returned when the run stopped at its 10-minute limit | 28 (errno 0) |
| `ftruncate(fd, 1 TiB)` | -1, errno Cannot allocate memory | not reached | 0 |

[POSIX](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_fallocate.html): "Upon successful completion, posix_fallocate() shall return zero; otherwise, an error number shall be returned to
indicate the error." It returns -1 and sets `errno` instead. .NET's tests that preallocate more space than the disk has
see no error.

**On 2.0.0.179, `posix_fallocate()` returns 0 for more space than the file system can give, after many minutes.** As the
1 TiB call above did not return, a check asked for 4 GiB on a QEMU machine with about 1.8 GiB of memory free (selected
lines; the full output is linked below):

```
[check] free pages at start: 459838
[check] open -> 3
[check] posix_fallocate(fd, 0, 4 GiB) -> 0 (errno 0 Success), 2864966 ms, free pages 459829 -> 427563
[check] fstat -> 0, st_size 4294967296, st_blocks 0, st_blksize 1024
[check] statvfs(/tmp) after -> 0, f_blocks 4194305, f_bfree 0, f_bavail 0, f_frsize 1024
[check] write(4 KiB at the end of the allocated range) -> 4096, free pages 427563
```

The call returned 0 after 48 minutes; the file has no blocks, and free memory fell by about 126 MiB. POSIX:
`posix_fallocate()` "shall ensure that any required storage for regular file data starting at offset and continuing
for len bytes is allocated on the file system storage media", and it "shall fail" with `[ENOSPC]` when "There is
insufficient free space remaining on the file system storage media". On 1.4 the same 4 GiB call returns -1 with
`errno` `ENOMEM` (the next check).

The time grows with the length on both versions, and on 2.0 nothing stops it early. Each call on a new file in `/tmp`:

| `posix_fallocate(fd, 0, len)`, `falloc-check.c` | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 |
|---|---|---|
| 1 MiB | 0 in 622 ms | 0 in 737 ms |
| 64 MiB | 0 in 15535 ms | 0 in 31011 ms |
| 512 MiB | 0 in 88794 ms, about 519 MiB of memory taken | 0 in 222118 ms, about 37 MiB of memory taken |
| 4 GiB | -1 (errno 12) in 191053 ms | 0 in 2864966 ms (`falloc4g-check.c`) |
| 1 TiB | -1 (errno 12) in 238309 ms | not returned when the run stopped (`fs-check.c`) |

At 2.0's 0.4-0.75 s per MiB, a 1 TiB call would take days. POSIX says nothing about the time, and neither manual
mentions `posix_fallocate()`. .NET does not call it; portable C code that preallocates files does.

Check: [`src/fs-check.c`](src/fs-check.c), [`src/falloc-check.c`](src/falloc-check.c),
[`src/falloc4g-check.c`](src/falloc4g-check.c), [`host/sdk-check.sh`](host/sdk-check.sh) (`NAME_MAX`); output
[`results/fs.kos.out`](results/fs.kos.out), [`results/2.0.0.179/fs.kos.out`](results/2.0.0.179/fs.kos.out), [`results/fs.linux.out`](results/fs.linux.out), [`results/falloc.kos.out`](results/falloc.kos.out),
[`results/2.0.0.179/falloc.kos.out`](results/2.0.0.179/falloc.kos.out) (stopped at the 10-minute limit during the 4 GiB call),
[`results/2.0.0.179/falloc4g.kos.out`](results/2.0.0.179/falloc4g.kos.out), [`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

Options: for `utimensat()`, allow it for the owner or with appropriate privileges, and fail with `EPERM` otherwise; for
`rename()`, fail with `ENAMETOOLONG`; for `posix_fallocate()`, return the error number, and fail with `ENOSPC` when the
space cannot be allocated; each as POSIX describes; keep the current behaviour and document it, with the time
`posix_fallocate()` takes; or another choice of yours.

<a id="b4"></a>
### B4. VfsRamFs crashes (NULL dereference in `inode_destructor`) under concurrent stat/readdir/unlink

Reported on the forum, with the fault report and the named call trace:
https://forum.kaspersky.com/topic/vfsramfs-crashes-null-dereference-in-inode_destructor-under-concurrent-statreaddirunlink-kasperskyos-ce-140102-59792/.

When several threads of one client create, stat, list and unlink files in `/tmp` at the same time, the prebuilt
VfsRamFs takes an unhandled page fault (a NULL pointer dereference in `inode_destructor`, reached from `_vfs_unlinkat`)
and the kernel terminates it, so its client loses its file system (`[VFS_CLIENT] Connection to vfs lost.` on every
later call). The reproducer with `THREADS=4` (12 worker threads) faulted in the first round in 3 of 3 runs; with
`THREADS=1` (3 worker threads) both runs completed. The SDK's "Limitations and known issues" page ([web page](https://support.kaspersky.com/kos-community-edition/2.0/limitations_and_known_problems)) states "a limit of 30
VFS clients and 5 threads per client", which `THREADS=4` exceeds; even so, a client over the limit takes the file
system server down rather than getting an error. A .NET program does not control how many of its threads call into
VFS (the runtime starts threads of its own); one of six full runs of .NET's System.IO.FileSystem tests lost its file
system this way.

**The KasperskyOS team's answer** (in the topic, 2026-09-26): "In the next release of the KasperskyOS Community Edition
SDK, this issue will be resolved." On 1.4, as a workaround, VfsRamFs can be made single-threaded through its
environment. `EXTRA_ENV` replaces VfsRamFs's default environment, so the backend line is restated:

```cmake
set_target_properties (precompiled_vfs::VfsRamFs PROPERTIES EXTRA_ENV "
    VFS_SERVER_MAX_THREADS_PER_CLIENT: 1
    VFS_SERVER_MAX_THREADS_PER_PROCESS: 1
    VFS_FILESYSTEM_BACKEND: server:kl.VfsRamFs")
```

With it, the reproducer with `THREADS=4` completed all 20 rounds in 2 of 2 runs, and a third run, stopped by a time
limit at round 11, had no fault either. The single-threaded server is slow for this load: about 2.5 minutes per round
under QEMU.

**On 2.0.0.179, fixed.** The reproducer, built for 2.0 by its `sdk2/` directory, with `THREADS=4` and VfsRamFs's
default environment (no workaround): all 20 rounds completed in 2 of 2 runs, about 20 minutes each, with no fault. Full
runs of .NET's System.IO.FileSystem tests with VfsRamFs's default thread settings had no VfsRamFs fault either.

Check: the reproducer, https://github.com/caraioniurie47/kos-vfsramfs-repro (plain C, needs only the SDK).

Nothing to choose: fixed in 2.0. Listed so that this list is complete.

<a id="b5"></a>
### B5. `shutdown()` on an unconnected socket succeeds

| Check (`net-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `shutdown(unconnected TCP socket, SHUT_RDWR)` | 0 | 0 | -1, errno Transport endpoint is not connected |
| `shutdown(bound unconnected UDP socket, SHUT_RDWR)` | 0 | 0 | -1, errno Transport endpoint is not connected |
| `  then poll(POLLIN, 0)` | 1 | 1 | 1 |
| `  then revents` | 0x1 | 0x1 | 0x11 |
| `  then recvmsg(1-byte buffer, MSG_DONTWAIT)` | 0 | 0 | -1, errno Resource temporarily unavailable |

[POSIX](https://pubs.opengroup.org/onlinepubs/9799919799/functions/shutdown.html) says `shutdown()` "shall fail" with `ENOTCONN` when "The socket is not connected." In NetBSD 10's source it
succeeds too: its [`soshutdown()`](https://github.com/NetBSD/src/blob/984c8a44149ca20397329137080f3eac31b2245d/sys/kern/uipc_socket.c#L1666-L1684) has no connection check. Its `shutdown(2)` man page does list `ENOTCONN`. On the
UDP socket, a non-blocking receive afterwards returns 0 bytes, as if an empty datagram had arrived, where Linux's fails
with `EAGAIN` (both report the socket readable).

.NET's `Socket.Disconnect` on an unconnected socket does not throw. When .NET closes a UDP socket that a receive is
blocked on, it calls shutdown(SHUT_RDWR) to wake that receive ([`SafeSocketHandle.Unix.cs`](https://github.com/dotnet/runtime/blob/1cddd530157ff15811616f9c48fd4bde2b877173/src/libraries/System.Net.Sockets/src/System/Net/Sockets/SafeSocketHandle.Unix.cs#L234-L241)); what the woken receive
returns was not measured on either system.

Check: [`src/net-check.c`](src/net-check.c); output [`results/net.kos.out`](results/net.kos.out), [`results/2.0.0.179/net.kos.out`](results/2.0.0.179/net.kos.out),
[`results/net.linux.out`](results/net.linux.out).

Options: fail with `ENOTCONN`, as POSIX describes; keep NetBSD's behaviour and document it, with what a receive returns
afterwards; or another choice of yours.

<a id="b6"></a>
### B6. A socket from `accept4()` inherits `O_NONBLOCK` from a non-blocking listener

| Check (`net-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `accept4(non-blocking listener, SOCK_CLOEXEC): O_NONBLOCK` | set | set | clear |

[POSIX.1-2024](https://pubs.opengroup.org/onlinepubs/9799919799/functions/accept4.html) says the new socket's `O_NONBLOCK` "shall be determined solely by the flag argument", and NetBSD's
`accept4()` (its [`paccept()`](https://github.com/NetBSD/src/blob/984c8a44149ca20397329137080f3eac31b2245d/sys/kern/uipc_syscalls.c#L300-L324)) clears the inherited flag; only `accept()` may inherit it ("it is implementation-defined
whether O_NONBLOCK will be set on the file description created by accept()").

.NET clears the flag after `accept()` on macOS and FreeBSD, but on a system with `accept4` it assumes Linux behaviour. On
KasperskyOS its synchronous reads on accepted sockets then failed with "Operation timed out" (its blocking-mode
`EAGAIN`), until my port cleared the flag there too.

Check: [`src/net-check.c`](src/net-check.c); output [`results/net.kos.out`](results/net.kos.out), [`results/2.0.0.179/net.kos.out`](results/2.0.0.179/net.kos.out),
[`results/net.linux.out`](results/net.linux.out).

Options: take `O_NONBLOCK` from `accept4()`'s flags only, as POSIX requires and NetBSD does; keep the inheritance and
document it; or another choice of yours.

<a id="b7"></a>
### B7. `poll()` fails the whole call with `EBADF` for a closed descriptor instead of setting `POLLNVAL` in its entry

| Check (`net-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `poll({open socket, closed descriptor}, 2, 0)` | -1, errno Bad file descriptor | -1, errno Bad file descriptor | 2 |
| `  revents` | 0x0, 0x0 | 0x0, 0x0 | 0x14, 0x20 |

[POSIX](https://pubs.opengroup.org/onlinepubs/9799919799/functions/poll.html): "poll() or ppoll() shall set the POLLHUP, POLLERR, and POLLNVAL flag in revents if the condition is true", where
POLLNVAL means "The specified fd value is not an open file descriptor."; `EBADF` is not among its errors. The
requirement is the "shall set": POSIX describes this condition and its outcome, a flag in the entry, so failing the whole
call is not one of the additional errors [XSH 2.3](https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html#tag_16_03) allows. NetBSD 10's [`pollscan`](https://github.com/NetBSD/src/blob/984c8a44149ca20397329137080f3eac31b2245d/sys/kern/sys_select.c#L568-L598) sets
`POLLNVAL` for such an entry and goes on, so the whole-call failure comes from KasperskyOS's layer.

An event loop that polls many sockets while other threads close them gets whole-call failures instead of per-descriptor
`POLLNVAL`. My port polls each descriptor alone after a failure to find the closed one. The POSIX implementation
specifics page says only that invalid handles combined with a negative (unlimited) timeout make `poll()` fail with
`EINVAL`; the program above uses a zero timeout.

Check: [`src/net-check.c`](src/net-check.c); output [`results/net.kos.out`](results/net.kos.out), [`results/2.0.0.179/net.kos.out`](results/2.0.0.179/net.kos.out),
[`results/net.linux.out`](results/net.linux.out).

Options: set `POLLNVAL` in the entry, as POSIX requires and NetBSD does; keep the current behaviour and document it; or
another choice of yours.

<a id="b8"></a>
### B8. A connected UDP socket cannot be disconnected

| Check (`net3-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `UDP connect(127.0.0.1:<peer port>)` | 0 | 0 | 0 |
| `  then connect(AF_UNSPEC)` | -1, errno Address family not supported by protocol family | -1, errno Address family not supported by protocol family | 0 |
| `  then connect(0.0.0.0:0)` | -1, errno Can't assign requested address | -1, errno Can't assign requested address | 0 |

POSIX [`connect()`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/connect.html): "If the sa_family member of address is AF_UNSPEC, the socket's peer address shall be reset." Here it
fails with `EAFNOSUPPORT`, and connecting to `0.0.0.0:0` fails too. .NET's test of connecting a datagram socket twice
fails with "Can't assign requested address".

Check: [`src/net3-check.c`](src/net3-check.c); output [`results/net3.kos.out`](results/net3.kos.out), [`results/2.0.0.179/net3.kos.out`](results/2.0.0.179/net3.kos.out),
[`results/net3.linux.out`](results/net3.linux.out).

Options: reset the peer on `AF_UNSPEC`, as POSIX describes; keep the current behaviour and document it; or another
choice of yours.

<a id="b9"></a>
### B9. A zero-length `recv()` on an empty non-blocking TCP socket returns 0

| Check (`net3-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `recv(non-blocking TCP, 0 bytes), nothing queued` | 0 | 0 | -1, errno Resource temporarily unavailable |

[POSIX](https://pubs.opengroup.org/onlinepubs/9799919799/functions/recv.html): "If no messages are available at the socket and O_NONBLOCK is set on the socket's file descriptor, recv() shall
fail and set errno to [EAGAIN] or [EWOULDBLOCK]". A return of 0 reads as end of stream. NetBSD's [`soreceive`](https://github.com/NetBSD/src/blob/984c8a44149ca20397329137080f3eac31b2245d/sys/kern/uipc_socket.c#L1266-L1270) returns 0
for a zero-length request before that check, so this one is inherited. No failing .NET test traced to it.

Check: [`src/net3-check.c`](src/net3-check.c); output [`results/net3.kos.out`](results/net3.kos.out), [`results/2.0.0.179/net3.kos.out`](results/2.0.0.179/net3.kos.out),
[`results/net3.linux.out`](results/net3.linux.out).

Options: fail with `EAGAIN`, as POSIX describes; keep NetBSD's 0 and document it; or another choice of yours.

<a id="b10"></a>
### B10. `sendto()` of zero bytes on a UDP socket returns 0 and sends nothing

| Check (`net3-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `UDP sendto(0 bytes)` | 0 | 0 | 0 |
| `UDP sendto(1 byte)` | 1 | 1 | 1 |
| `  receiver's first recvfrom (0: the empty one)` | 1 | 1 | 0 |
| `  receiver's second recvfrom` | -1, errno Resource temporarily unavailable | -1, errno Resource temporarily unavailable | 1 |
| `UDP sendmsg(one empty iovec)` | 0 | 0 | 0 |
| `UDP sendto(1 byte) after it` | 1 | 1 | 1 |
| `  then the receiver's first recvfrom` | 0 | 0 | 0 |
| `  then the receiver's second recvfrom` | 1 | 1 | 1 |

[POSIX](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sendto.html): "The sendto() function shall send a message through a connection-mode or connectionless-mode socket." Here it
returns 0, the size of an empty message, without sending one. `sendmsg()` with one empty iovec does send the empty
datagram, as the last four rows show. No failing .NET test traced to this (.NET sends datagrams with `sendmsg`).

Check: [`src/net3-check.c`](src/net3-check.c); output [`results/net3.kos.out`](results/net3.kos.out), [`results/2.0.0.179/net3.kos.out`](results/2.0.0.179/net3.kos.out),
[`results/net3.linux.out`](results/net3.linux.out).

Options: send the empty datagram, as POSIX describes; keep the current behaviour and document it; or another choice of
yours.

<a id="b11"></a>
### B11. `IOV_MAX` is 10, and `sendmsg()` with more iovecs fails with `EINVAL`

| Check (`net3-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `IOV_MAX` | 10 | 10 | 1024 |
| `UDP sendmsg with 11 one-byte iovecs` | -1, errno Invalid argument | -1, errno Invalid argument | 11 |
| `UDP sendmsg with 1024 one-byte iovecs` | -1, errno Invalid argument | -1, errno Invalid argument | 1024 |
| `UDP sendmsg with 1025 one-byte iovecs` | -1, errno Invalid argument | -1, errno Invalid argument | -1, errno Message too long |

POSIX's [`sendmsg()`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sendmsg.html) lists "[EMSGSIZE] ... the msg_iovlen member of the msghdr structure pointed to by message is less
than or equal to 0 or is greater than {IOV_MAX}." as a "shall fail" error ([XSH 2.3](https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html#tag_16_03)); NetBSD returns `EMSGSIZE` there. The value 10
itself is documented as the VFS IPC's `MaxIovecsCount` (p. 869 of the 1.4 PDF, p. 832 of the 2.0 PDF; [web page](https://support.kaspersky.com/kos-community-edition/2.0/system_limits)), but it is below `_XOPEN_IOV_MAX` (16).
.NET's datagram sends and receives with more than 10 buffers fail with "Invalid argument", where its tests expect
"message too long".

Check: [`src/net3-check.c`](src/net3-check.c), [`host/sdk-check.sh`](host/sdk-check.sh) (`IOV_MAX`); output
[`results/net3.kos.out`](results/net3.kos.out), [`results/2.0.0.179/net3.kos.out`](results/2.0.0.179/net3.kos.out), [`results/net3.linux.out`](results/net3.linux.out), [`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

Options: fail with `EMSGSIZE`, as POSIX and NetBSD do; keep `EINVAL` and document it; or another choice of yours. And
could the documentation state the value 10 with `IOV_MAX`?

<a id="b12"></a>
### B12. A thread blocked in `read()` or `recvmsg()` returns with `errno` -3 when another thread closes the socket

| Check (`net3-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `read(TCP) blocked, another thread closes the socket` | -1, errno -3 (Unknown error: -3) | -1, errno -3 (Unknown error: -3) | still blocked 5 s later |
| `recvmsg(UDP) blocked, another thread closes the socket` | -1, errno -3 (Unknown error: -3) | -1, errno -3 (Unknown error: -3) | still blocked 5 s later |
| `non-blocking write(TCP, 4 KiB) until it stops` | -1, errno Resource temporarily unavailable after 13 writes that sent data | -1, errno Resource temporarily unavailable after 23 writes that sent data | -1, errno Resource temporarily unavailable after 646 writes that sent data |
| `write(TCP) blocked, another thread closes the socket` | 4096 | 4096 | still blocked 5 s later |

POSIX's [`<errno.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/errno.h.html) defines the error numbers as "distinct positive values"; -3 is not an error number at all, so
`strerror()` and any error mapping cannot name it. A 64 KiB `write()` made once the send buffer is full (the last
non-blocking write failed with `EAGAIN`), and still blocked 1 s later, returns 4096 when the socket is closed, not an
error. No failing .NET test traced to it.

Check: [`src/net3-check.c`](src/net3-check.c); output [`results/net3.kos.out`](results/net3.kos.out), [`results/2.0.0.179/net3.kos.out`](results/2.0.0.179/net3.kos.out),
[`results/net3.linux.out`](results/net3.linux.out).

Options: return a positive error number, such as `EBADF`; keep -3 and document it; or another choice of yours.

<a id="b13"></a>
### B13. At the 512-descriptor limit `open()` of `/dev/null` fails with `ENFILE`; `getrlimit`/`setrlimit` are not implemented

| Check (`sys-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `sysconf(_SC_OPEN_MAX)` | 512 | 512 | 10240 |
| `getrlimit(RLIMIT_NOFILE)` | -1, errno Invalid argument | -1, errno Invalid argument | 0 |
| `  open() of a /tmp file until it fails` | 509, errno Too many open files | 509, errno Too many open files | 5000 |
| `  open() of /dev/null until it fails` | 509, errno Too many open files in system | 509, errno Too many open files in system | 5000 |
| `setrlimit(RLIMIT_NOFILE, soft limit 256)` | -1, errno Function not implemented | -1, errno Function not implemented | 0 |
| `setrlimit(RLIMIT_NOFILE, soft and hard limit 1024)` | -1, errno Function not implemented | -1, errno Function not implemented | 0 |
| `  then open() of a /tmp file until it fails (limit 1024)` | 509, errno Too many open files | 509, errno Too many open files | 1020, errno Too many open files |

A process cannot have more than 512 descriptors (`OPEN_MAX` in the SDK's `limits.h`), and nothing changes that:
[`getrlimit(RLIMIT_NOFILE)`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getrlimit.html) fails with `EINVAL` and [`setrlimit()`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setrlimit.html) with `ENOSYS`. POSIX.1-2024 moved both functions, with
`RLIMIT_NOFILE`, from the XSI option into the Base. At the limit, [`open()`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/open.html) of a `/tmp` file fails with `EMFILE`, but `open()` of `/dev/null` with
`ENFILE`, which POSIX describes as "The maximum allowable number of files is currently open in the system."; for "All
file descriptors available to the process are currently open." it gives `EMFILE`. .NET raises the soft limit at startup,
which fails silently here, and the System.Net.Sockets tests sometimes reach the limit when their test classes overlap.
(Linux's 5000 above is the program's own cap.)

Check: [`src/sys-check.c`](src/sys-check.c), [`host/sdk-check.sh`](host/sdk-check.sh) (`OPEN_MAX`); output
[`results/sys.kos.out`](results/sys.kos.out), [`results/2.0.0.179/sys.kos.out`](results/2.0.0.179/sys.kos.out), [`results/sys.linux.out`](results/sys.linux.out),
[`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

Options: fail `open()` of `/dev/null` with `EMFILE` at the process's limit, as POSIX describes; keep `ENFILE` and
document it; or another choice of yours. And could the documentation give the limit and the status of `getrlimit()` and
`setrlimit()`, or could they be implemented as POSIX.1-2024 has them?

<a id="b14"></a>
### B14. `sigaction()` installs a handler for `SIGKILL` and `SIGSTOP`

| Check (`sig-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `sigaction(SIGKILL, handler)` | 0; handler read back: installed | 0; handler read back: installed | -1, errno Invalid argument; handler read back: not installed |
| `sigaction(SIGSTOP, handler)` | 0; handler read back: installed | 0; handler read back: installed | -1, errno Invalid argument; handler read back: not installed |
| `sigaction(SIGTERM, handler)` | 0; handler read back: installed | 0; handler read back: installed | 0; handler read back: installed |
| `sigaction(SIGUSR1, handler)` | 0; handler read back: installed | 0; handler read back: installed | 0; handler read back: installed |

[POSIX](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigaction.html): "The sigaction() function shall fail if: [EINVAL] The sig argument is not a valid signal number or an attempt is
made to catch a signal that cannot be caught or ignore a signal that cannot be ignored." Since the kernel delivers no
signals, the handler never runs; the effect is that code asking `sigaction` whether a signal can be handled gets the
wrong answer.

Check: [`src/sig-check.c`](src/sig-check.c); output [`results/sig.kos.out`](results/sig.kos.out), [`results/2.0.0.179/sig.kos.out`](results/2.0.0.179/sig.kos.out),
[`results/sig.linux.out`](results/sig.linux.out).

Options: fail with `EINVAL` for these two, as POSIX describes; keep the current behaviour and document that `sigaction`
accepts any signal; or another choice of yours.

<a id="b15"></a>
### B15. `mprotect()` to read, write and execute fails with `ENOMEM`, where POSIX and the manual give `ENOTSUP`

```
[check] mmap(64 KiB, R|W|X, private anon)            -> Cannot allocate memory
[check] mprotect(64 KiB R|X anon, to R|W|X)          -> -1, errno Cannot allocate memory
[check] mprotect(R|W anon, code written, to R|X)     -> 0
[check]   calling that code (mov w0, #42; ret)       -> 42
```

POSIX lists "[ENOTSUP] The implementation does not support the combination of accesses requested in the prot argument."
among the errors [`mprotect()`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mprotect.html) "shall fail" with, and the `mprotect()` row of the POSIX limitations says "it will only
return the -1 value and will assign the ENOTSUP value to the errno variable" (p. 452 of the 1.4 PDF, p. 462 of the 2.0 PDF; [web page](https://support.kaspersky.com/kos-community-edition/2.0/posix_uns_ifaces)). Writing a page,
then switching it to `PROT_READ | PROT_EXEC`, works.

**On 2.0.0.179:** the same output.

Check: [`src/mem-check.c`](src/mem-check.c), its last four lines; output [`results/mem.kos.out`](results/mem.kos.out), [`results/2.0.0.179/mem.kos.out`](results/2.0.0.179/mem.kos.out).

Options: fail with `ENOTSUP`, as POSIX and the `mprotect()` row describe; keep `ENOMEM` and correct the row; or another
choice of yours. And could the row say which kernel configurations prohibit write-and-execute and whether the QEMU image
uses one?

<a id="b16"></a>
### B16. `dlerror()` is declared `const char *`

```
$ grep -nw dlerror include/dlfcn.h
36:const char *dlerror(void);
```

[POSIX](https://pubs.opengroup.org/onlinepubs/9799919799/functions/dlerror.html) declares `char *dlerror(void);`. Code that stores the result in a `char *` gets a qualifier warning, an error where
warnings are errors (as in .NET's native build).

**On 2.0.0.179:** the same declaration, at line 37.

Check: [`host/sdk-check.sh`](host/sdk-check.sh); output [`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

Options: declare it as POSIX has it; keep it and document the difference; or another choice of yours.

## Documentation errors

<a id="e1"></a>
### E1. `link()` fails with `ENOSYS`, though the manual lists it

| Check (`fs-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `link("/tmp/plain.txt", "/tmp/hard.txt")` | -1, errno Function not implemented | -1, errno Function not implemented | 0 |

The manual's table "Functions implemented by the vfs::lib_fs library" (pp. 96-97 of the 1.4 PDF, p. 103 of the 2.0 PDF; [web page](https://support.kaspersky.com/kos-community-edition/2.0/vfs_overview)) lists `link()` without
qualification, and the POSIX support limitations ([web page](https://support.kaspersky.com/kos-community-edition/2.0/posix_uns_ifaces)) list `mkfifo()` as a stub but not `link()`.

Check: [`src/fs-check.c`](src/fs-check.c); output [`results/fs.kos.out`](results/fs.kos.out), [`results/2.0.0.179/fs.kos.out`](results/2.0.0.179/fs.kos.out),
[`results/fs.linux.out`](results/fs.linux.out).

Options: implement hard links on VfsRamFs; keep `ENOSYS` and document it, with `mkfifo()`; or another choice of yours.

<a id="e2"></a>
### E2. `sendfile()` from a file to a TCP socket fails with `EINVAL`, though the manual lists it

| Check (`net-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `sendfile(tcp socket, file, &offset, 512)` | -1, errno Invalid argument | -1, errno Invalid argument | 512 |

`sendfile()` is declared and defined, so configure checks find it, and the manual's table "Functions implemented by the
vfs::lib_fs library" (pp. 96-97; 2.0: p. 103; [web page](https://support.kaspersky.com/kos-community-edition/2.0/vfs_overview)) lists it. `sendfile()` is not POSIX, and NetBSD has none. `Socket.SendFile` in .NET
threw "Invalid argument"; my port now copies with `read` and `write` on KasperskyOS.

Check: [`src/net-check.c`](src/net-check.c), [`host/sdk-check.sh`](host/sdk-check.sh) (the declaration and definition); output
[`results/net.kos.out`](results/net.kos.out), [`results/2.0.0.179/net.kos.out`](results/2.0.0.179/net.kos.out), [`results/net.linux.out`](results/net.linux.out), [`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

Options: make `sendfile` work for sockets; leave it out of the headers and libraries; document the destinations it
supports; or another choice of yours.

<a id="e3"></a>
### E3. The manual documents IPv6 for `kos_net.h`, but this SDK package's network stack has no IPv6

| Check (`net-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `socket(AF_INET6, SOCK_STREAM, 0)` | -1, errno Address family not supported by protocol family | -1, errno Address family not supported by protocol family | 3 |

The manual documents IPv6 configuration for `kos_net.h` ("Configure the available network interfaces with IPv4 and IPv6
addressing.", `configure_net_iface6()`, pp. 124-125 of the 1.4 PDF, pp. 128-129 of the 2.0 PDF; [web page](https://support.kaspersky.com/kos-community-edition/2.0/kos_net_api)). The network stack has no IPv6 library:
`rumpConfig.cmake` adds `rump::rumpnet_netinet6` only if that target exists, and the sysroot has no
`librumpnet_netinet6.a`. Portable code and test suites often assume at least an IPv6 loopback; many of .NET's socket
tests do.

Check: [`src/net-check.c`](src/net-check.c), [`host/sdk-check.sh`](host/sdk-check.sh) (`rumpConfig.cmake` and the `netinet6` files); output
[`results/net.kos.out`](results/net.kos.out), [`results/2.0.0.179/net.kos.out`](results/2.0.0.179/net.kos.out), [`results/net.linux.out`](results/net.linux.out), [`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

Is IPv6 left out of the Community Edition on purpose, or not yet available? Options: build the stack with IPv6; state in
the networking documentation that the IPv6 functions have no stack behind them; or another choice of yours.

<a id="e4"></a>
### E4. Two typos in log messages

```
$ strings $SDK/sysroot-aarch64-kos/lib/libvfs_remote.a | grep -m1 connetion
Can't establish IPC connetion to VFS server

$ strings $SDK/sysroot-aarch64-kos/lib/libem_transport_lib.a | grep -m1 succesfully
EM transport succesfully connected
```

**On 2.0.0.179, partly fixed:** "connetion" is still there. `libem_transport_lib.a` no longer has "succesfully", nor any
string matching `transport.*connected`, so that message was reworded or removed.

Check: [`host/toolchain-check.sh`](host/toolchain-check.sh); output [`results/toolchain-check.out`](results/toolchain-check.out), [`results/2.0.0.179/toolchain-check.out`](results/2.0.0.179/toolchain-check.out).

## Undocumented behaviour

<a id="u1"></a>
### U1. A process has uid 0 but not root's rights, yet `unlink()` removes directories; directories lose the setuid and setgid bits

| Check (`fs-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `geteuid()` | 0 | 0 | 0 |
| `open(0444 file it created, O_WRONLY)` | -1, errno Permission denied | -1, errno Permission denied | 5 |
| `unlink(empty directory)` | 0 | 0 | -1, errno Is a directory |
| `  stat(that directory) afterwards` | -1, errno No such file or directory | -1, errno No such file or directory | 0 |
| `chmod(file, 06755)` | mode 106755 | mode 106755 | mode 106755 |
| `chmod(directory, 06755)` | mode 40755 | mode 40755 | mode 46755 |

Portable code and test suites commonly take uid 0 to mean "permission checks are bypassed"; .NET's tests do, and
expected writes to read-only files to succeed. The POSIX support limitations ([web page](https://support.kaspersky.com/kos-community-edition/2.0/posix_uns_ifaces)) and POSIX implementation specifics
([web page](https://support.kaspersky.com/kos-community-edition/2.0/posix_ifaces_impl_features)) pages do
not say what uid 0 means on KasperskyOS.

Yet the same process may remove an empty directory with [`unlink()`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/unlink.html), which POSIX allows only if "the process has
appropriate privileges and the implementation supports using unlink() on directories". Which privileges are
appropriate is for the implementation to define. The image's security policy grants everything; whether that is what
lets the call through was not tested (the check never ran under a stricter policy). In .NET, `File.Delete` on a directory deletes it instead of throwing.

Setuid and setgid bits are kept on files but dropped on directories.

Check: [`src/fs-check.c`](src/fs-check.c); output [`results/fs.kos.out`](results/fs.kos.out), [`results/2.0.0.179/fs.kos.out`](results/2.0.0.179/fs.kos.out),
[`results/fs.linux.out`](results/fs.linux.out).

What decides a process's privileges on KasperskyOS: for writing a read-only file, for `unlink()` of a directory, the
security policy or something else? Options: give uid 0 root's rights, as on Linux; keep the current behaviour and
document what uid 0 means, what lets `unlink()` remove a directory, and which mode bits each VFS keeps; or another
choice of yours.

<a id="u2"></a>
### U2. VfsRamFs's `statvfs()` reports no free space

| Check (`fs-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `statvfs(/tmp): f_blocks, f_bfree, f_bavail` | 1, 0, 0 (f_frsize 1024) | 1, 0, 0 (f_frsize 1024) | 16384, 16384, 16384 (f_frsize 4096) |
| `  the same with a 4 MiB file in /tmp` | 4098, 0, 0 | 4098, 0, 0 | 16384, 15360, 15360 |

`f_bfree` and `f_bavail` stay 0, and `f_blocks` counts only the space in use. [POSIX](https://pubs.opengroup.org/onlinepubs/9799919799/functions/statvfs.html) allows that ("It is unspecified
whether all members of the statvfs structure have meaningful values on all file systems."), but the manual says
`statvfs()` gives the "number of available blocks" (p. 460 of the 1.4 PDF, p. 470 of the 2.0 PDF; [web page](https://support.kaspersky.com/kos-community-edition/2.0/sys_inf_libc)), and nothing says VfsRamFs reports none.
.NET's `DriveInfo` reports 0 bytes free and the space in use as the drive's size, and its test that expects free space on
`/` failed.

Check: [`src/fs-check.c`](src/fs-check.c); output [`results/fs.kos.out`](results/fs.kos.out), [`results/2.0.0.179/fs.kos.out`](results/2.0.0.179/fs.kos.out),
[`results/fs.linux.out`](results/fs.linux.out).

Options: report the free memory as the free space, as Linux's tmpfs reports its limit; keep the current values and
document them; or another choice of yours.

<a id="u3"></a>
### U3. `readdir()` of a directory removed after `opendir()` fails with `ENOENT`

| Check (`sys-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `rmdir of that directory while the stream is open` | 0 | 0 | 0 |
| `  then readdir()` | NULL, errno No such file or directory | NULL, errno No such file or directory | NULL (end, errno unchanged) |

Linux ends the stream. [POSIX](https://pubs.opengroup.org/onlinepubs/9799919799/functions/readdir.html) lists "[ENOENT] The current position of the directory stream is invalid.", so this is
allowed, but code that reads NULL plus an `errno` as an error sees one: .NET's directory enumeration throws when the
directory is removed under it.

Check: [`src/sys-check.c`](src/sys-check.c); output [`results/sys.kos.out`](results/sys.kos.out), [`results/2.0.0.179/sys.kos.out`](results/2.0.0.179/sys.kos.out),
[`results/sys.linux.out`](results/sys.linux.out).

Options: end the stream, as Linux does; keep `ENOENT` and document it; or another choice of yours.

<a id="u4"></a>
### U4. `localhost` resolves only from a hosts file given to VfsNet

| Check (`net-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `getaddrinfo("localhost")` | 7, No address associated with hostname | 7, No address associated with hostname | 0 |
| `  mkdir("/etc") in the program's file system` | 0 | 0 | not run |
| `  then /etc/hosts created, bytes of 127.0.0.1 localhost` | 20 | 20 | not run |
| `  again, with /etc/hosts in the program's file system` | 7, No address associated with hostname | 7, No address associated with hostname | not run |

`getaddrinfo()` runs in VfsNet (the manual lists it under "Functions implemented by the vfs::lib_net library", p. 97; 2.0: p. 103; [web page](https://support.kaspersky.com/kos-community-edition/2.0/vfs_overview)),
which reads `/etc/hosts` from its own file systems: an `/etc/hosts` written to the program's file system (the middle rows) changes nothing (on
Linux that file is the system's, so the check does not write it). The manual's examples list a hosts file among VfsNet's configuration files, but nothing
says that name resolution, `localhost` included, needs one; [RFC 6761 (6.3)](https://www.rfc-editor.org/rfc/rfc6761#section-6.3) recommends that resolvers always return the
loopback address for `localhost`. The program above gives VfsNet no hosts file.

.NET's socket tests connect to `localhost` by name, and 26 of them failed. After I gave VfsNet a ROMFS `/etc/hosts` with
`127.0.0.1 localhost` (as the SDK's `net_with_separate_vfs` example does for its own hosts), those tests, run again
alone, no longer failed: 20 passed, and 4 skipped because `localhost` has no IPv6 address (the 2 IPv6 cases among the 26
are no longer run, as there is no IPv6).

**On 2.0.0.179:** the same. The 2.0 manual adds an example `product_init.yaml.in` (p. 557; [web page](https://support.kaspersky.com/kos-community-edition/2.0/cmake_yaml_templates)) that gives VfsNet
`ALLOWED_FS_PATHS: "/etc/hosts;/etc/resolv.conf"`; still nothing says that `localhost` needs a hosts file.

Check: [`src/net-check.c`](src/net-check.c); output [`results/net.kos.out`](results/net.kos.out), [`results/2.0.0.179/net.kos.out`](results/2.0.0.179/net.kos.out),
[`results/net.linux.out`](results/net.linux.out).

Options: resolve `localhost` without a hosts file, as RFC 6761 recommends; document that name resolution, `localhost`
included, needs a hosts file for VfsNet or DNS; or another choice of yours.

<a id="u5"></a>
### U5. `FIONREAD` on a UDP socket counts 16 bytes more than the datagram

| Check (`net3-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `ioctl(UDP, FIONREAD), one 3-byte datagram queued` | 19 | 19 | 3 |
| `ioctl(TCP, FIONREAD), 3 bytes queued` | 3 | 3 | 3 |

16 is the size of the sender's `sockaddr_in`, which NetBSD's source also counts: its [`sbappendaddr()`](https://github.com/NetBSD/src/blob/984c8a44149ca20397329137080f3eac31b2245d/sys/kern/uipc_socket2.c#L1023-L1081)
runs `sballoc()` over the address mbuf along with the data, adding its length to the socket buffer's byte count; not measured on NetBSD.
`FIONREAD` is not in POSIX. .NET's `Socket.Available` overstates on a UDP socket.

Check: [`src/net3-check.c`](src/net3-check.c); output [`results/net3.kos.out`](results/net3.kos.out), [`results/2.0.0.179/net3.kos.out`](results/2.0.0.179/net3.kos.out),
[`results/net3.linux.out`](results/net3.linux.out).

Options: report the payload bytes, as Linux does; keep NetBSD's count and document it; or another choice of yours.

<a id="u6"></a>
### U6. `SO_SNDBUF` and `SO_RCVBUF` of 0 fail with `EINVAL`

| Check (`net3-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `setsockopt(TCP, SO_SNDBUF, 0)` | -1, errno Invalid argument | -1, errno Invalid argument | 0 |
| `setsockopt(TCP, SO_RCVBUF, 0)` | -1, errno Invalid argument | -1, errno Invalid argument | 0 |
| `setsockopt(UDP, SO_SNDBUF, 0)` | -1, errno Invalid argument | -1, errno Invalid argument | 0 |

POSIX gives no minimum: the [`setsockopt()`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/setsockopt.html) option "requests that the buffer space ... be set to the value": it lists `EINVAL` for
"The specified option is invalid at the specified socket level", and [XSH 2.3](https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html#tag_16_03) lets an implementation use a listed error
under other circumstances when it can be handled as the described one, which a rejected value can. NetBSD does the same:
its `sosetopt1()`
([`uipc_socket.c`:1802-1809](https://github.com/NetBSD/src/blob/984c8a44149ca20397329137080f3eac31b2245d/sys/kern/uipc_socket.c#L1802-L1809),
NetBSD 10) fails these options with `EINVAL` for "Values < 1"; from the source,
not measured on NetBSD. Linux accepts the call (what size it then uses is not shown here). Setting .NET's
`SendBufferSize` or `ReceiveBufferSize` to 0 throws.

Check: [`src/net3-check.c`](src/net3-check.c); output [`results/net3.kos.out`](results/net3.kos.out), [`results/2.0.0.179/net3.kos.out`](results/2.0.0.179/net3.kos.out),
[`results/net3.linux.out`](results/net3.linux.out).

What is the smallest accepted value? Options: let the call succeed for 0, as Linux's does; keep NetBSD's `EINVAL` and
document the accepted range; or another choice of yours.

<a id="u7"></a>
### U7. `getsockopt()` with a NULL buffer and length 0 fails with `EINVAL`

| Check (`net3-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `getsockopt(SO_RCVBUF, NULL, option_len 0)` | -1, errno Invalid argument | -1, errno Invalid argument | 0 |
| `getsockopt(SO_RCVBUF, buffer, option_len 0)` | 0 | 0 | 0 |

POSIX does not allow a null `option_value`, so the result is undefined ("If an argument to a function has an invalid
value, such as ... a null pointer, the behavior is undefined", [XSH 2.1.1](https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html#tag_16_01_01)) and failing is allowed. NetBSD's
[`getsockopt()`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/getsockopt.html) accepts it with length 0 and copies nothing, as Linux does. .NET's `GetSocketOption` with an empty buffer
threw "Invalid argument"; my port passes a one-byte buffer.

Check: [`src/net3-check.c`](src/net3-check.c); output [`results/net3.kos.out`](results/net3.kos.out), [`results/2.0.0.179/net3.kos.out`](results/2.0.0.179/net3.kos.out),
[`results/net3.linux.out`](results/net3.linux.out).

Options: accept a NULL buffer with length 0, as NetBSD does; keep `EINVAL` and document it; or another choice of yours.

<a id="u8"></a>
### U8. On TCP, `sendmsg()` of more than 64 KiB fails with `EMSGSIZE`, even on a blocking socket; `send()` and `writev()` send part

| Check (`net2-check.c`, non-blocking loopback TCP, the peer reads nothing) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `sendmsg(tcp, 65536 bytes)` | 49152 | 57344 | 65536 |
| `sendmsg(tcp, 65537 bytes)` | -1, errno Message too long | -1, errno Message too long | 65537 |
| `sendmsg(tcp, 1 MiB)` | -1, errno Message too long | -1, errno Message too long | 1048576 |
| `sendmsg(tcp, 1 MiB, msg_name = peer)` | -1, errno Message too long | -1, errno Message too long | 1048576 |
| `send(tcp, 1 MiB)` | 49152 | 57344 | 1048576 |
| `writev(tcp, 1 MiB)` | 49152 | 57344 | 1048576 |

| Check (`net2-check.c`, blocking loopback TCP, the peer reads everything) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `sendmsg(blocking tcp, 1 MiB), the peer reading` | -1, errno Message too long | -1, errno Message too long | 1048576 |
| `  bytes the peer read after sendmsg` | 0 | 0 | 1048576 |
| `send(blocking tcp, 1 MiB), the peer reading` | 65536 | 65536 | 1048576 |
| `  bytes the peer read after send` | 65536 | 65536 | 1048576 |

The limit is on the length, not on buffer space: on a blocking socket whose peer is reading, the call fails and sends
nothing, where [`send()`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/send.html) on the same sockets sends part. POSIX's `EMSGSIZE` is for "The message is too large to be sent
all at once (as the socket requires)", which a stream socket does not require; [XSH 2.3](https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html#tag_16_03) lets an implementation use a
listed error under other circumstances when it can be handled as the described one, which, in my judgement, a caller
can, by sending less.
NetBSD's `sosend` fails with `EMSGSIZE` only for a socket that sends atomically, or for control data over the buffer size
([`uipc_socket.c`:966-968](https://github.com/NetBSD/src/blob/984c8a44149ca20397329137080f3eac31b2245d/sys/kern/uipc_socket.c#L966-L968),
NetBSD 10), so the limit comes from KasperskyOS's layer, and the documentation does not mention it. .NET sends with a
destination address, or from several buffers, through `sendmsg`; a 10 MB
`SendToAsync` in its tests hung until my port retried with 64 KiB.

Check: [`src/net2-check.c`](src/net2-check.c); output [`results/net2.kos.out`](results/net2.kos.out), [`results/2.0.0.179/net2.kos.out`](results/2.0.0.179/net2.kos.out),
[`results/net2.linux.out`](results/net2.linux.out).

Options: send part of the data, as `send()` does, or block until space is available, as POSIX describes for a blocking socket;
keep the limit and document it; or another choice of yours.

<a id="u9"></a>
### U9. `accept4()` fails with `EACCES` when `address_len` is over 128

| Check (`net3-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `accept4(TCP listener, address_len 128)` | 0 | 0 | 0 |
| `accept4(TCP listener, address_len 244)` | -1, errno Permission denied | -1, errno Permission denied | 0 |
| `accept4(AF_UNIX listener, address_len 128)` | 0 | 0 | 0 |
| `accept4(AF_UNIX listener, address_len 244)` | -1, errno Permission denied | -1, errno Permission denied | 0 |

POSIX [`accept()`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/accept.html): `address_len` "on input specifies the length of the supplied sockaddr structure", and a longer
address "shall be truncated"; a larger buffer is legal. `EACCES` is not among `accept()`'s errors, but [XSH 2.3](https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html#tag_16_03) lets an
implementation "generate additional errors unless explicitly disallowed". NetBSD shortens the returned length and never fails on it. 128
matches the VFS IPC's `MaxSockAddrSize` (p. 869; 2.0: p. 832; [web page](https://support.kaspersky.com/kos-community-edition/2.0/system_limits)). .NET passes a 244-byte buffer when accepting on an `AF_UNIX` listener,
so its named-pipe server failed with "Permission denied" in 120 of the 128 named-pipe tests, until my port passed 128.

Check: [`src/net3-check.c`](src/net3-check.c); output [`results/net3.kos.out`](results/net3.kos.out), [`results/2.0.0.179/net3.kos.out`](results/2.0.0.179/net3.kos.out),
[`results/net3.linux.out`](results/net3.linux.out).

Options: accept a larger buffer and return the address's length, as POSIX and NetBSD do; keep the limit and document
it; or another choice of yours.

<a id="u10"></a>
### U10. `setsockopt()` fails with `ECONNRESET` once the peer has reset the connection

| Check (`net2-check.c`, loopback TCP) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `setsockopt(SO_LINGER {1, 0}) after the peer reset` | -1, errno Connection reset by peer | -1, errno Connection reset by peer | 0 |
| `setsockopt(SO_KEEPALIVE, 1) after the peer reset` | -1, errno Connection reset by peer | -1, errno Connection reset by peer | 0 |
| `close() after that` | 0 | 0 | 0 |

NetBSD's [`tcp_ctloutput`](https://github.com/NetBSD/src/blob/984c8a44149ca20397329137080f3eac31b2245d/sys/netinet/tcp_usrreq.c#L257-L277) returns `ECONNRESET` when the socket has no protocol control block; that a reset connection
has none is my reading, not traced. .NET's abortive close sets `SO_LINGER {1, 0}` and did not close the descriptor when
that failed, so each socket reset by its peer leaked one, until my port treated `ECONNRESET` there as success.

Check: [`src/net2-check.c`](src/net2-check.c); output [`results/net2.kos.out`](results/net2.kos.out), [`results/2.0.0.179/net2.kos.out`](results/2.0.0.179/net2.kos.out),
[`results/net2.linux.out`](results/net2.linux.out).

Options: let `setsockopt()` succeed on a reset socket, as Linux does; keep the current behaviour and document it; or
another choice of yours.

<a id="u11"></a>
### U11. `accept()` with no descriptor left drops the pending connection

| Check (`net3-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `connect(blocking TCP), then no descriptor left` | 0 | 0 | 0 |
| `  accept()` | -1, errno Too many open files | -1, errno Too many open files | -1, errno Too many open files |
| `  then the client's recv() (0: connection closed)` | 0 | 0 | -1, errno Resource temporarily unavailable |
| `  then accept() with descriptors free` | -1, errno Resource temporarily unavailable | -1, errno Resource temporarily unavailable | 0 |

At the limit `accept()` fails with `EMFILE`, as expected, but the pending connection is dropped: the client reads EOF,
and a later `accept()` with descriptors free finds nothing. [POSIX](https://pubs.opengroup.org/onlinepubs/9799919799/functions/accept.html) lists "[EMFILE] All file descriptors available to the
process are currently open." and says nothing about the connection. [NetBSD 10's source](https://github.com/NetBSD/src/blob/984c8a44149ca20397329137080f3eac31b2245d/sys/kern/uipc_syscalls.c#L174-L234) allocates the descriptor before
it takes the connection off the queue, so there the connection stays queued, as it does on Linux. A .NET server's
`Accept` at the limit fails "Too many open files", and its client then sees the connection closed. (On Linux the check
lowers the limit to 256 first.)

Check: [`src/net3-check.c`](src/net3-check.c); output [`results/net3.kos.out`](results/net3.kos.out), [`results/2.0.0.179/net3.kos.out`](results/2.0.0.179/net3.kos.out),
[`results/net3.linux.out`](results/net3.linux.out).

Options: keep the connection queued, as NetBSD and Linux do; document that it is dropped; or another choice of yours.

<a id="u12"></a>
### U12. `uname()` returns constants rather than the product and version

```
[check] uname() -> 0
[check] sysname  "KOS"
[check] nodename "kos-host"
[check] release  "1.0"
[check] version  "1.0"
[check] machine  "aarch64"
[check] done
```

The product and version are available at build time (`include/platform/version.h` has
`PRODUCT_NAME "KasperskyOS-Community-Edition-Qemu"` and `PRODUCT_VERSION "1.4.0.102"`), but `uname()` does not tell a
program which KasperskyOS it runs on. [POSIX](https://pubs.opengroup.org/onlinepubs/9799919799/functions/uname.html): "The format of each member is implementation-defined."

**On 2.0.0.179:** the same output, while `version.h` has `PRODUCT_NAME "KasperskyOS-Community-Edition-Qemu-arm64"` and
`PRODUCT_VERSION "2.0.0.179"`.

Check: [`src/uname-check.c`](src/uname-check.c); output [`results/uname.kos.out`](results/uname.kos.out), [`results/2.0.0.179/uname.kos.out`](results/2.0.0.179/uname.kos.out).

Options: have `uname()` report the kernel's product and version; document another supported way for a program to learn
them; or another choice of yours.

<a id="u13"></a>
### U13. `pthread_condattr_init()` fails with `EINVAL` on memory that holds an initialized attribute

| Check (`sys-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `pthread_condattr_init(zeroed memory)` | 0 | 0 | 0 |
| `pthread_condattr_init(that attribute again)` | 22, errno Invalid argument | 22, errno Invalid argument | 0 |
| `pthread_condattr_init(a byte copy of it)` | 22, errno Invalid argument | 22, errno Invalid argument | 0 |
| `pthread_condattr_init(after pthread_condattr_destroy)` | 0 | 0 | 0 |

[POSIX](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pthread_condattr_init.html) leaves re-initializing an initialized attribute undefined, so this is allowed. But a stack variable that happens
to reuse the bytes of an earlier attribute that was never destroyed looks initialized, and the call fails on it; .NET's
garbage collector declares such an attribute on the stack, and my port now zeroes it first.

Check: [`src/sys-check.c`](src/sys-check.c); output [`results/sys.kos.out`](results/sys.kos.out), [`results/2.0.0.179/sys.kos.out`](results/2.0.0.179/sys.kos.out),
[`results/sys.linux.out`](results/sys.linux.out).

Options: skip the check; keep it and document it; or another choice of yours.

<a id="u14"></a>
### U14. A program linked with the VFS client, in an image without a VFS server, waits about 10 s at startup

A program linked with the VFS client (`vfs::client`) and started in an image that has no VFS program logs this, and only
then prints the program's own lines (`checks.Check`; Einit's and DCM's lines above it are left out):

```
[2026-10-02T09:52:29.639][Info][checks.Check][14:14][CRT0] Initing main app: statically-linked, PIE.
[2026-10-02T09:52:39.710][Error][checks.Check][14:14][VFS_CLIENT] DCM read pub queue failed, error Retcode 0x90000014: Space General, Facility 0, Error code 20 (Timeout)
[2026-10-02T09:52:39.713][Fatal][checks.Check][14:14][VFS_CLIENT] Can't establish IPC connetion to VFS server
[2026-10-02T09:52:39.718][Error][checks.Check][14:14][VFS_INIT] Failed to create backend "client", error 61 (Connection refused)
[2026-10-02T09:52:39.719][Info][checks.Check][14:14][CRT0] VFS filesystem and network backends initialized with stub (related calls will return EIO)
[check] CLOCK_MONOTONIC on entry to main()                         -> 11734 ms
[check] open("/tmp/x", O_CREAT | O_RDWR)                           -> -1, errno Input/output error
[check] done
```

About 10 seconds pass between the start and the fallback to the stub (10.08 s here); `main()` runs after it, and file
calls then fail with `EIO`, as the runtime's line says (the program's one `open()` does). The program prints to stderr; a
line it writes to stdout does not appear. The SDK's `examples/hello/hello/src/hello.c` says why: "To work with the
standard output stream stdout, a program that supports the VFS must be added to the solution."

**On 2.0.0.179, not testable:** every 2.0 image runs MinimalKOS's own VFS program, `kl.VfsEntity`, so an image
without a VFS program cannot be built. Built the same way (no VFS environment, no automatic VFS connection), the
program's VFS client initialized 0.24 s after it started, and its `open()` and a line to stdout worked: it reached
`kl.VfsEntity`, which is a different setup from the one above.

Check: [`src/novfs-check.c`](src/novfs-check.c) (`CHECK=novfs`: an image with no VFS program); output
[`results/novfs.kos.out`](results/novfs.kos.out), [`results/2.0.0.179/novfs.kos.out`](results/2.0.0.179/novfs.kos.out).

Options: have the client fail at once when no VFS server is configured for it; document the wait; or another choice of
yours.

<a id="u15"></a>
### U15. Reserving address space with `PROT_NONE` takes physical memory and seconds; `MADV_DONTNEED` and `MADV_FREE` free nothing

The programs read free physical memory with `KnGroupStatGetParam(GROUP_PARAM_MEM_FREE)`, in pages, on QEMU with `-m 2048`
(as the SDK's `sim` target starts it).

```
[check] mmap(512 MiB, PROT_NONE, private anon)       -> ok, 52923 ms, free pages 358470 -> 227142 (513 MiB taken)
[check] mmap(512 MiB, PROT_NONE, ... | MAP_NORESERVE) -> ok, 141 ms, free pages 358440 -> 358178 (1 MiB taken)
```

The first reservation took 53 s and 513 MiB. With `MAP_NORESERVE` the same reservation is fast and takes (almost) no memory. .NET's garbage collector reserves a large
range with `PROT_NONE` at startup and commits parts of it later. Without `MAP_NORESERVE` a .NET program on KasperskyOS
took tens of seconds to start and held over 500 MiB it never used; with `MAP_NORESERVE` (my port's choice now) startup
is normal.

```
[check] madvise(64 MiB touched, MADV_DONTNEED)       -> 0, free pages 339350 -> 339350
[check]   first byte afterwards                      -> 1 (want 0 after MADV_DONTNEED)
[check] madvise(the same 64 MiB, MADV_FREE)          -> 0, free pages 339350 -> 339350
[check] munmap(the same 64 MiB)                      -> 0, free pages 339350 -> 355766
```

Only `munmap` returns the pages. To give memory back without losing the reservation, the GC on Linux maps `PROT_NONE`
over the range with `MAP_FIXED` (documented as not supported on KasperskyOS: [web page](https://support.kaspersky.com/kos-community-edition/2.0/posix_ifaces_impl_features)) or calls `madvise(MADV_FREE)`, so my port
cannot return freed heap memory to the system short of unmapping it. [POSIX](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mmap.html) does not say when memory is committed, and
`madvise` and `MAP_NORESERVE` are not POSIX.

**On 2.0.0.179:** the same behaviour. The plain reservation took 18611 ms and 512 MiB, the `MAP_NORESERVE` one 59 ms
and under 1 MiB; `MADV_DONTNEED` and `MADV_FREE` freed no page, the first byte still read 1, and `munmap` returned the pages.

Check: [`src/mem-check.c`](src/mem-check.c); output [`results/mem.kos.out`](results/mem.kos.out), [`results/2.0.0.179/mem.kos.out`](results/2.0.0.179/mem.kos.out).

Options: make `PROT_NONE` reservations lazy by default, so that only committed pages cost memory, and have
`MADV_DONTNEED` release the pages, so that the next access sees zero-filled pages; keep the current behaviour and
document it; or another choice of yours.

<a id="u16"></a>
### U16. Memory from a `MAP_NORESERVE` mapping is taken at the first write, and when none is left the kernel ends the process (1.4; documented in 2.0)

256 MiB at a time, every page written, first with plain read-write mappings (all unmapped afterwards), then as a garbage
collector commits memory, reserving with `PROT_NONE` and `MAP_NORESERVE` and then calling
`mprotect(PROT_READ | PROT_WRITE)`:

```
[check] free pages at start: 358440 (1400 MiB)
[check] 1. mmap(256 MiB, R|W) #1 -> ok, free pages 290169; writing it
[check] 1. mmap(256 MiB, R|W) #2 -> ok, free pages 224454; writing it
[check] 1. mmap(256 MiB, R|W) #3 -> ok, free pages 159049; writing it
[check] 1. mmap(256 MiB, R|W) #4 -> ok, free pages 93384; writing it
[check] 1. mmap(256 MiB, R|W) #5 -> ok, free pages 27720; writing it
[check] 1. mmap(256 MiB, R|W) #6 -> Cannot allocate memory, free pages 27715
[check] 1. all unmapped, free pages 356036
[check] 2. reserve, mprotect(R|W) #1 -> ok, free pages 355908; writing it
[check] 2. reserve, mprotect(R|W) #2 -> ok, free pages 290244; writing it
[check] 2. reserve, mprotect(R|W) #3 -> ok, free pages 224580; writing it
[check] 2. reserve, mprotect(R|W) #4 -> ok, free pages 158915; writing it
[check] 2. reserve, mprotect(R|W) #5 -> ok, free pages 93251; writing it
[check] 2. reserve, mprotect(R|W) #6 -> ok, free pages 27587; writing it
[VMM  ] Unhandled Overcommit, address: 0x578c4000
Terminating task.
```

A plain mapping takes its memory when it is made, and the sixth fails cleanly with `ENOMEM`. A `MAP_NORESERVE` mapping
takes memory as it is written (the free count drops only by the step before), `mprotect()` succeeds for the sixth with
about 107 MiB free, and the write ends the process; no call reports the shortage. Because of [U15](#u15), my port of
.NET reserves with `MAP_NORESERVE`, so a .NET program that runs out of memory is ended instead of getting
`OutOfMemoryException`; one of .NET's cryptography tests, which allocates 512 MiB, ended this way. The documentation says
nothing about when memory is committed.

**On 2.0.0.179, documented, with a choice of policies.** The same check:

```
[check] free pages at start: 459967 (1796 MiB)
[check] 1. mmap(256 MiB, R|W) #1 -> ok, free pages 394298; writing it
[check] 1. mmap(256 MiB, R|W) #2 -> ok, free pages 328960; writing it
[check] 1. mmap(256 MiB, R|W) #3 -> ok, free pages 263573; writing it
[check] 1. mmap(256 MiB, R|W) #4 -> ok, free pages 198185; writing it
[check] 1. mmap(256 MiB, R|W) #5 -> ok, free pages 132831; writing it
[check] 1. mmap(256 MiB, R|W) #6 -> ok, free pages 67364; writing it
[check] 1. mmap(256 MiB, R|W) #7 -> ok, free pages 1994; writing it
[check] 1. mmap(256 MiB, R|W) #8 -> Cannot allocate memory, free pages 2227
[check] 1. all unmapped, free pages 461993
[check] 2. reserve, mprotect(R|W) #1 -> ok, free pages 461937; writing it
[check] 2. reserve, mprotect(R|W) #2 -> ok, free pages 396923; writing it
[check] 2. reserve, mprotect(R|W) #3 -> ok, free pages 331254; writing it
[check] 2. reserve, mprotect(R|W) #4 -> ok, free pages 265570; writing it
[check] 2. reserve, mprotect(R|W) #5 -> ok, free pages 199891; writing it
[check] 2. reserve, mprotect(R|W) #6 -> ok, free pages 134224; writing it
[check] 2. reserve, mprotect(R|W) #7 -> ok, free pages 68557; writing it
[check] 2. reserve, mprotect(R|W) #8 -> ok, free pages 2890; writing it
[OCMT] Task 139 (Check.1) thread 139 is waiting for free pages
```

The writing thread now waits for free pages instead of ending the process, and nothing more happened in the 60 s before
the run was stopped. The 2.0 manual lists this among the critical changes ("the kernel will now wait for free pages to appear
instead of throwing an EXCEPTION_OVERCOMMIT error", p. 33; [web page](https://support.kaspersky.com/kos-community-edition/2.0/breaking_changes)) and documents the kernel startup parameter
`vmm.overcommit` (p. 171; [web page](https://support.kaspersky.com/kos-community-edition/2.0/kernel_command_line)): by default "the thread is locked while waiting for the necessary amount of physical memory
to be freed"; `exception` throws `EXCEPTION_OVERCOMMIT` (with it, the check above ended as on 1.4: `Unhandled
Overcommit`, `Terminating task.`); `disabled` commits at once (the sixth reservation then fails with `ENOMEM`, and the
program ends normally). Flags of `task.h` set the policy per process (p. 246; [web page](https://support.kaspersky.com/kos-community-edition/2.0/libkos_task_high_api)). That answers this finding.

Check: [`src/oom-check.c`](src/oom-check.c); output [`results/oom.kos.out`](results/oom.kos.out), [`results/2.0.0.179/oom.kos.out`](results/2.0.0.179/oom.kos.out),
[`results/2.0.0.179/oom-overcommit-exception.kos.out`](results/2.0.0.179/oom-overcommit-exception.kos.out),
[`results/2.0.0.179/oom-overcommit-disabled.kos.out`](results/2.0.0.179/oom-overcommit-disabled.kos.out) (the
parameter set by `-D KOS_KERNEL_CMDLINE=vmm.overcommit=<mode>`).

Options (for 1.4; answered by 2.0): fail `mprotect()` (or the first write) with `ENOMEM` when a `MAP_NORESERVE` range
cannot be backed; keep the current behaviour and document the commit and overcommit policy; or another choice of yours.

<a id="u17"></a>
### U17. `mprotect()` cannot make a read-only page of the program image writable

| Check (`sys-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `mprotect(.rodata page, PROT_NONE), then PROT_READ` | 0, then 0 | 0, then 0 | 0, then 0 |
| `mprotect(.rodata page, PROT_READ\|PROT_WRITE), then PROT_READ` | -1, errno Permission denied, then 0 | -1, errno Permission denied, then 0 | 0, then 0 |
| `mprotect(anonymous page, PROT_READ\|PROT_WRITE), then PROT_READ` | 0, then 0 | 0, then 0 | 0, then 0 |

Raising fails with `EACCES`; lowering and restoring its protection works. POSIX leaves [`mprotect()`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mprotect.html) unspecified for
memory not mapped by `mmap()`, and lists "[EACCES] The prot argument specifies a protection that violates the access
permission the process has to the underlying memory object", which describes this. .NET's NativeAOT runtime can keep its
stack-protection cookie on a read-only page that it makes writable once at startup; my port leaves that off on
KasperskyOS, as .NET does on Apple platforms and OpenBSD.

Check: [`src/sys-check.c`](src/sys-check.c); output [`results/sys.kos.out`](results/sys.kos.out), [`results/2.0.0.179/sys.kos.out`](results/2.0.0.179/sys.kos.out),
[`results/sys.linux.out`](results/sys.linux.out).

Options: allow it; state in the `mprotect()` row that a segment of the program image cannot be given more access than it
was loaded with, and that the call then fails with `EACCES`; or another choice of yours.

<a id="u18"></a>
### U18. `aarch64-kos-clang` ignores `-static-pie` with a warning and links dynamically

```
$ $SDK/toolchain/bin/aarch64-kos-clang -static-pie t.c -o t-static-pie
clang: warning: argument unused during compilation: '-static-pie' [-Wunused-command-line-argument]

$ file t-static-pie | sed "s|.*: ||"
ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV), dynamically linked, interpreter /lib/ld.elf_so.2, BuildID[xxHash]=0451fbf9f1a52a6b, not stripped

$ $SDK/toolchain/bin/aarch64-kos-clang -static t.c -o t-static

$ file t-static | sed "s|.*: ||"
ELF 64-bit LSB pie executable, ARM aarch64, version 1 (SYSV), static-pie linked, BuildID[xxHash]=e93af8a7216db7c9, not stripped
```

(`t.c` is `int main(void) { return 0; }`.) A build that asks for `-static-pie` gets a dynamically linked executable and
only a warning; `-static` produces the static PIE. Why it matters: `-static-pie` is how build systems ask for a static
position-independent executable; .NET's NativeAOT build passes it for a static executable; my port passes `-static`
instead on KasperskyOS.

**On 2.0.0.179:** the same output, with other build IDs.

Check: [`host/toolchain-check.sh`](host/toolchain-check.sh); output [`results/toolchain-check.out`](results/toolchain-check.out), [`results/2.0.0.179/toolchain-check.out`](results/2.0.0.179/toolchain-check.out).

If `-static-pie` is left out on purpose, why? Options: accept `-static-pie` as the same as `-static`; reject it with an
error; document it; or another choice of yours.

<a id="u19"></a>
### U19. `toolchain/bin` has unprefixed `clang`, `clang++` and `clang-17` that target KasperskyOS

```
$ ls $SDK/toolchain/bin | grep -E "^clang(-[0-9]+)?$|^clang\+\+$"
clang
clang++
clang-17

$ $SDK/toolchain/bin/clang-17 --version | head -2
clang version 17.0.6
Target: aarch64-unknown-kos
```

Host build scripts that look for `clang-<N>` or `clang` on `PATH` (.NET's does) find these and build host tools for
KasperskyOS, which fails later in confusing ways. The Visual Studio Code extension puts the directory there by default
("Add the path $SDK_PREFIX/toolchain/bin to the PATH variable. Enabled by default.", p. 74 of the 1.4 manual, p. 85 of the 2.0 manual; [web page](https://support.kaspersky.com/kos-community-edition/2.0/301291)).

**On 2.0.0.179:** the same output.

Check: [`host/toolchain-check.sh`](host/toolchain-check.sh); output [`results/toolchain-check.out`](results/toolchain-check.out), [`results/2.0.0.179/toolchain-check.out`](results/2.0.0.179/toolchain-check.out).

Options: ship only the `aarch64-kos-` names; document that `toolchain/bin` must not be on `PATH` during host builds; or
another choice of yours.

<a id="u20"></a>
### U20. 2.0: a `close()` during another thread's `poll()` ends the process

New in 2.0.0.179. A thread polls eight UDP sockets for up to 10 ms at a time while the main thread replaces them,
closing each old socket at once, 3000 times, so some closes land inside a `poll()` that holds the descriptor. On 1.4
the program completes, and most `poll()` calls fail: with `EBADF`, as in [B7](#b7), and also with `EINVAL` or `EACCES`
(the last three lines of its output):

```
[check] closes while polling: 3000 (close failed 0)
[check] poll calls 2890: returned >= 0 359 (POLLNVAL entries 6); -1 EBADF 111, EINVAL 1186, EACCES 1234, other 0 (last errno 0)
[check] done
```

On 2.0 the program prints its first two lines (the sockets made, the poller thread started), then the kernel reports
this on the console and the process ends, before the 500th close:

```
==============================================================================
                           Access to invalid handle
==============================================================================

    EIID     : checks.Check
    Name     : Check.1
    Path     : /data/packages.unpacked/Check/v0.0.0/bin/Check
    Handle   : 0x158
    Type     : 0x10004
    Status   : 0
    Expected : 0x1
    Current  : 0x0

Trace of access to invalid handle:

Relocation base: <0x0000000679654000>
Stack bounds: 0x000006440a143000 0x000006440a243000
Call Trace (User Mode):
    [<0x000000067971e900>] CallEx+0x50
    [<0x0000000679717b24>] KnNoticeSubscribeToObject+0x94
    [<0x000000067977e93c>] poll+0x4ec
    [<0x00000006796f81cc>] Poller+0x11c
    [<0x00000006797c69e4>] pthread__errorfunc+0x1c4
[2026-10-20T03:26:35.305][Info][kl.ExecMgrEntity][24:124][EXECMGR][executable_runner.cpp:CompletedExecutable:180] [Check.1] Executable completed
[2026-10-20T03:26:35.310][Info][kl.ExecMgrEntity][24:124][EXECMGR][executable_runner.cpp:CompletedExecutable:197] [Check.1] Executable completed with: exitStatus=ExitNormal, exitCode=1, exitReason=(0x00, Ok)
```

With the kernel startup parameter `handle.disable_detect_invalid_access`, 2.0 completes the program:

```
[check] closes while polling: 3000 (close failed 0)
[check] poll calls 2995: returned >= 0 2985 (POLLNVAL entries 1); -1 EBADF 8, EINVAL 2, EACCES 0, other 0 (last errno 0)
[check] done
```

The 2.0 manual describes detecting the use of invalid handles as part of a handle sanitizer that "must be activated
separately in each process for which checks for invalid handles and/or handle leaks are required", with the
environment variable `KOS_HANDLE_SANITIZER=True` (p. 592; [web page](https://support.kaspersky.com/kos-community-edition/2.0/debugging_handle_sanitizer)), and "Invalid handle usage is detected automatically in all
processes in which the handle sanitizer is enabled." (p. 593); this image does not set the variable. The parameter
above "Disables the handle sanitizer functionality for detecting the use of invalid handles." (p. 170; [web page](https://support.kaspersky.com/kos-community-edition/2.0/kernel_command_line)). So the detection is on in every
process by default and ends the process, which the manual does not say; its "Critical changes in version 2.0" ([web page](https://support.kaspersky.com/kos-community-edition/2.0/breaking_changes)) do not
mention it. POSIX's [`close()`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/close.html) and [`poll()`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/poll.html) say nothing about a descriptor closed while another thread polls it. NetBSD's
[`fd_close()`](https://github.com/NetBSD/src/blob/984c8a44149ca20397329137080f3eac31b2245d/sys/kern/kern_descrip.c#L685-L690) calls such a close "typically an application error - the descriptor is being closed while still in use.
(Or just a threaded application trying to unblock its thread that sleeps in (say) accept()).", and waits for the other
thread's use to end; the process lives.

An event loop polls descriptors that other threads close; .NET's does. On 2.0, runs of .NET's System.IO.FileSystem and
System.Net.Sockets tests were ended this way within minutes, until my port made `close()` wait for a `poll()` that holds
the descriptor.

Check: [`src/pollclose-check.c`](src/pollclose-check.c); output [`results/pollclose.kos.out`](results/pollclose.kos.out),
[`results/2.0.0.179/pollclose.kos.out`](results/2.0.0.179/pollclose.kos.out) with the console log
[`results/2.0.0.179/pollclose-qemu.log`](results/2.0.0.179/pollclose-qemu.log),
[`results/2.0.0.179/pollclose-nodetect.kos.out`](results/2.0.0.179/pollclose-nodetect.kos.out) (built with
`-D KOS_KERNEL_CMDLINE=handle.disable_detect_invalid_access`).

Options: document that the detection is on in every process by default and ends it, naming
`handle.disable_detect_invalid_access`; report a descriptor closed during `poll()` in its entry, as [B7](#b7) asks for a
closed one, instead of treating it as the use of an invalid handle; keep the detection under `KOS_HANDLE_SANITIZER`
only, as the manual describes it; or another choice of yours.

<a id="u21"></a>
### U21. 2.0: `build_kos_qemu_image()` no longer builds an image, and the breaking changes do not say so

New in 2.0.0.179. This repository's image project for 1.4 ([`CMakeLists.txt`](CMakeLists.txt), which calls
`build_kos_qemu_image()`), configured and built with SDK 2.0:

```
$ $SDK/toolchain/bin/cmake -B build -D CMAKE_TOOLCHAIN_FILE=$SDK/toolchain/share/toolchain-aarch64-kos.cmake -D CHECK=uname > configure.log 2>&1; echo "exit code $?"
exit code 0

$ grep -B1 -A4 "be added" configure.log | sed "s|$SDK|\$SDK|g"
CMake Warning at $SDK/toolchain/share/cmake/Modules/platform/kos_image/image.cmake:1273 (message):
  Target aarch64-kos won't be added:
  $SDK/sysroot-aarch64-kos/../libexec/aarch64-kos/kos-qemu
  or
  $SDK/sysroot-aarch64-kos/../libexec/aarch64-kos/kos-qemu.ld
  not found

$ $SDK/toolchain/bin/cmake --build build --target kos-qemu-image > build.log 2>&1; echo "exit code $?"
exit code 2

$ grep -m1 "No rule" build.log
gmake: *** No rule to make target 'kos-qemu-image'.  Stop.

$ ls -A $SDK/libexec | wc -l
0

$ grep -rln "^ *function *(build_kos_qemu_image" $SDK --include=*.cmake | sed "s|$SDK|\$SDK|"
$SDK/toolchain/share/cmake/Modules/platform/kos_image/image.cmake

$ grep -rl build_kos_qemu_image $SDK/examples --include=CMakeLists.txt | wc -l
0

$ grep -rl generate_qemu_target $SDK/examples --include=CMakeLists.txt | wc -l
31
```

The function is still defined, but it adds no target, with only a warning, and the build fails at `make`. The examples
now boot a prebuilt MinimalKOS with `generate_qemu_target()`. The 2.0 documentation does not say that an image around
Einit alone can no longer be built. The 1.4 manual says "build_kos_qemu_image() creates the target for building a
solution image for QEMU." (p. 550), and its web page `cmake_build_qemu` ([web page](https://support.kaspersky.com/kos-community-edition/1.4/cmake_build_qemu)) has no 2.0 version; the 2.0 page "Critical
changes in version 2.0" ([web page](https://support.kaspersky.com/kos-community-edition/2.0/breaking_changes)) names neither the function nor such images, and the closest statement in the 2.0 "What's new" ([web page](https://support.kaspersky.com/kos-community-edition/2.0/whats_new)) is "The internal architecture and the process of loading OS
components and system programs have been modified."; the 2.0 manual's "Critical changes in version 2.0" (p. 32) say
nothing on building images, and the manual never names `build_kos_qemu_image`. One leftover: the PAL test steps still
say "select the target named `kos-qemu-image-PalTest<N>-sim`" (p. 73; [web page](https://support.kaspersky.com/kos-community-edition/2.0/vscode_ext_use)). This repository's [`sdk2/`](sdk2/) builds the same programs the
2.0 way.

Check: [`host/image-check.sh`](host/image-check.sh), which runs the commands above; output
[`results/2.0.0.179/image-check.out`](results/2.0.0.179/image-check.out).

Options: list the change among the breaking changes, with a pointer to the new model; make `build_kos_qemu_image()` stop
at configure with that pointer instead of a warning; or another choice of yours.

## Missing features

<a id="m1"></a>
### M1. The stack honours `SO_REUSEPORT` (NetBSD's 0x0200), but no header defines it

| Check (`net3-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `second UDP bind to one port, SO_REUSEADDR on both` | -1, errno Address already in use | -1, errno Address already in use | 0 |
| `SO_REUSEPORT defined by <sys/socket.h>` | no | no | yes |
| `second UDP bind to one port, SO_REUSEPORT on both` | 0 (with 0x0200) | 0 (with 0x0200) | 0 |

`include/rump/rumpdefs.h` has `RUMP_SO_REUSEPORT 0x0200`, and setting 0x0200 lets two UDP sockets share a unicast port,
as on NetBSD; the SDK's `sys/socket.h` does not define the name (NetBSD 10's defines `SO_REUSEPORT` as 0x0200), so
portable code that tests `#ifdef SO_REUSEPORT` cannot use it. .NET could not let two UDP sockets share a port until my
port defined 0x0200.

Check: [`src/net3-check.c`](src/net3-check.c), [`host/sdk-check.sh`](host/sdk-check.sh); output
[`results/net3.kos.out`](results/net3.kos.out), [`results/2.0.0.179/net3.kos.out`](results/2.0.0.179/net3.kos.out), [`results/net3.linux.out`](results/net3.linux.out),
[`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

Options: define `SO_REUSEPORT` in `sys/socket.h`, as NetBSD does; leave it undefined and document the value; or another
choice of yours.

<a id="m2"></a>
### M2. `sysconf(_SC_PHYS_PAGES)` fails with `EINVAL` although `unistd.h` defines the name

| Check (`sys-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `sysconf(_SC_PHYS_PAGES)` | -1, errno Invalid argument | -1, errno Invalid argument | 4094840 |
| `sysconf(_SC_PAGESIZE)` | 4096 | 4096 | 4096 |

`_SC_PHYS_PAGES` is not in POSIX, so `EINVAL` is what [POSIX](https://pubs.opengroup.org/onlinepubs/9799919799/functions/sysconf.html) asks for a name the system does not support; but `unistd.h`
defines it, and portable code that finds the name uses it for the memory size. .NET's GC reads the memory size this way
at startup; my port reads `KnGroupStatGetParam(GROUP_PARAM_MEM_TOTAL)` instead.

Check: [`src/sys-check.c`](src/sys-check.c); output [`results/sys.kos.out`](results/sys.kos.out), [`results/2.0.0.179/sys.kos.out`](results/2.0.0.179/sys.kos.out),
[`results/sys.linux.out`](results/sys.linux.out).

Options: answer `_SC_PHYS_PAGES` from that statistic; remove the name from `unistd.h`; or another choice of yours.

<a id="m3"></a>
### M3. The sysroot ships OpenSSL libraries (1.1.1t; 3.5.7 in 2.0) but no OpenSSL headers

Commands run in `$SDK/sysroot-aarch64-kos`:

```
$ strings lib/libcrypto.a | grep -m1 "^OpenSSL 1"
OpenSSL 1.1.1t  7 Feb 2023

$ grep -h ^Version lib/pkgconfig/openssl.pc
Version: 1.1.1t

$ find . -name opensslv.h -o -type d -name openssl | wc -l
0
```

To build against these libraries I had to use the headers from the 1.1.1t release tarball, configured with OpenSSL's
defaults, and check by hand that their disabled-feature set matched the symbols in the libraries. And since [OpenSSL 1.1.1
reached its upstream end of life on 2023-09-11](https://openssl-library.org/post/2023-03-28-1.1.1-eol/), current software that needs newer APIs cannot use it (for example ML-KEM,
ML-DSA and SLH-DSA in .NET, which on KasperskyOS throw `PlatformNotSupportedException` with 1.1.1).

**On 2.0.0.179, partly fixed:** the libraries are OpenSSL 3.5.7, still without headers (the first command now matches
any version):

```
$ strings lib/libcrypto.a | grep -m1 "^OpenSSL [0-9]"
OpenSSL 3.5.7 9 Jun 2026

$ grep -h ^Version lib/pkgconfig/openssl.pc
Version: 3.5.7

$ find . -name opensslv.h -o -type d -name openssl | wc -l
0
```

Check: [`host/sdk-check.sh`](host/sdk-check.sh); output [`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

Could the SDK ship the headers generated by its own OpenSSL build? (1.4's second question, whether an OpenSSL 3.x build
was planned, is answered by 2.0.)

<a id="m4"></a>
### M4. `libcrypto.a` references a `getentropy()` that no SDK library defines and no libc header declares

```
$ $SDK/toolchain/bin/llvm-nm lib/libcrypto.a 2>/dev/null | grep -w getentropy
                 w getentropy

$ for a in lib/*.a; do $SDK/toolchain/bin/llvm-nm --defined-only $a 2>/dev/null | grep -qw -E "[TW] getentropy" && echo $a; done | wc -l
0

$ grep -rlw getentropy include --exclude-dir=boost | wc -l
0
```

With the weak reference unresolved, OpenSSL skips that source and seeds from a device file (`/dev/urandom` and others),
which is there when the image's VFS mounts `devfs`.

**On 2.0.0.179:** the same output, with OpenSSL 3.5.7's `libcrypto.a`; where 3.5.7 seeds from without `getentropy()` was
not checked.

Check: [`host/sdk-check.sh`](host/sdk-check.sh); output [`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

Options: provide `getentropy()` in libc (on `KosRandomGenerate`) and declare it in `unistd.h`, as glibc, musl and the
BSDs do, so that OpenSSL and other portable code seed from the kernel's generator; leave it out and document the entropy
source portable code should use; or another choice of yours.

<a id="m5"></a>
### M5. No GSSAPI or Kerberos library

`find . \( -iname "*gssapi*" -o -iname "libkrb5*" \) | wc -l` gives 0 in `$SDK/sysroot-aarch64-kos`. .NET's Negotiate
authentication has no Kerberos on KasperskyOS. Worth a line in the list of available libraries.

**On 2.0.0.179:** the same.

Check: [`host/sdk-check.sh`](host/sdk-check.sh); output [`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

Options: provide a GSSAPI library; list it as absent in the documentation; or another choice of yours.

<a id="m6"></a>
### M6. Functions declared in the headers but defined in no library

The documentation says "There is no XSI support or optional functionality" (POSIX support limitations, [web page](https://support.kaspersky.com/kos-community-edition/2.0/posix_uns_ifaces)). `setgrent`,
`getgrent`, `endgrent` are XSI and `shm_open` belongs to an option, so their absence is expected, but the headers still
declare them:

```
$ grep -nw -E "shm_open|setgrent|getgrent|endgrent" include/sys/mman.h include/grp.h
include/sys/mman.h:240:int	shm_open(const char *, int, mode_t);
include/grp.h:25:void endgrent(void);
include/grp.h:26:struct group *getgrent(void);
include/grp.h:31:void setgrent(void);

$ for f in shm_open setgrent getgrent endgrent; do n=0; for a in lib/*.a; do $SDK/toolchain/bin/llvm-nm --defined-only $a 2>/dev/null | grep -qw -E "[TW] $f" && n=$((n+1)); done; echo "$f: defined in $n archives"; done
shm_open: defined in 0 archives
setgrent: defined in 0 archives
getgrent: defined in 0 archives
endgrent: defined in 0 archives
```

Code that uses them compiles, and the link fails (for me in .NET's native code, whose group lookup calls `setgrent`,
`getgrent` and `endgrent`).

**On 2.0.0.179:** the same output; the 2.0 manual has the same sentence on XSI.

Check: [`host/sdk-check.sh`](host/sdk-check.sh); output [`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

Options: drop the four declarations; define the functions to fail with `ENOSYS` like the other unsupported interfaces;
or another choice of yours.

<a id="m7"></a>
### M7. `signal.h` lacks `SA_RESETHAND` and `SA_NODEFER`

```
$ grep -n "define SA_" include/strict/posix/signal.h
393:#define SA_NOCLDSTOP   0x0008
394:#define SA_RESTART  0x0002
395:#define SA_SIGINFO  0x0040

$ grep -rlw -E "SA_RESETHAND|SA_NODEFER" include | wc -l
0
```

POSIX.1-2024's [`<signal.h>`](https://pubs.opengroup.org/onlinepubs/9799919799/basedefs/signal.h.html) says "The <signal.h> header shall also define the following symbolic constants:", and the
list includes `SA_RESETHAND` and `SA_NODEFER`, marked CX, not XSI; its Issue 7 change history says "The SA_RESETHAND,
SA_RESTART, SA_SIGINFO, SA_NOCLDWAIT, and SA_NODEFER constants are moved from the XSI option to the Base.", so the
documented absence of XSI does not cover them. Signals other than `SIGTERM` are not delivered, so this only breaks
compilation.

**On 2.0.0.179:** the same output.

Check: [`host/sdk-check.sh`](host/sdk-check.sh); output [`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

Do you count this as a gap, or as a defect against POSIX's "shall also define"? Options: define them in `signal.h`, as
POSIX describes; list them as absent in the documentation; or another choice of yours.

<a id="m8"></a>
### M8. There is no C `<uchar.h>` (C11, POSIX.1-2024), and no library defines its functions

```
$ find . -name uchar.h | wc -l
0

$ grep -rlw -E "mbrtoc16|c16rtomb|mbrtoc32|c32rtomb" include | wc -l
0

$ $SDK/toolchain/bin/llvm-nm --defined-only lib/libc.a 2>/dev/null | grep -cw -E "[TW] (mbrtoc16|c16rtomb|mbrtoc32|c32rtomb)"
0
```

The only `uchar.h` in the SDK is libc++'s C++ wrapper (`toolchain/include/c++/v1/uchar.h`), and the compiler reports
`__STDC_VERSION__` 201710L. ICU 78 [includes `<uchar.h>` from C code](https://github.com/unicode-org/icu/blob/21d1eb0f306e1141c10931e914dfc038c06121da/icu4c/source/common/unicode/ptypes.h#L55-L61) on [every platform but Darwin and old Cygwin](https://github.com/unicode-org/icu/blob/21d1eb0f306e1141c10931e914dfc038c06121da/icu4c/source/common/unicode/platform.h#L722-L729), so its
cross build stopped until I patched it.

**On 2.0.0.179:** the same output.

Check: [`host/sdk-check.sh`](host/sdk-check.sh); output [`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

Options: provide `<uchar.h>` and its functions; document that it is absent; or another choice of yours.

<a id="m9"></a>
### M9. `fallocate()` without its `FALLOC_FL_*` flags

`fallocate()` is declared and defined, but no header defines its flags, so code that finds `fallocate` and then uses
`FALLOC_FL_KEEP_SIZE` does not compile:

```
$ grep -nw fallocate include/fcntl.h
58:int fallocate(int fd, int mode, off_t offset, off_t len);

$ $SDK/toolchain/bin/llvm-nm --defined-only lib/libc.a 2>/dev/null | grep -w -E "[TW] fallocate"
0000000000000000 W fallocate

$ grep -rn FALLOC_FL_ include | wc -l
0
```

**On 2.0.0.179:** the same output.

Check: [`host/sdk-check.sh`](host/sdk-check.sh); output [`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

Options: define the `FALLOC_FL_*` flags the implementation accepts; document which modes it supports; or another choice
of yours.

<a id="m10"></a>
### M10. Headers that portable code often probes are absent

Absent: `sys/syscall.h`, `sys/statfs.h`, `sys/vfs.h`, `endian.h`, `elf.h`, `malloc.h`, `mntent.h`, `sys/epoll.h`,
`sys/event.h`, `sys/inotify.h`, `linux/futex.h`; `unistd.h` has no `_SC_AVPHYS_PAGES`. NetBSD-style equivalents are
there (`sys/endian.h`, `sys/exec_elf.h`). A documentation page mapping the usual Linux headers to the ones to use on
KasperskyOS would save porters time.

**On 2.0.0.179:** the same.

Check: [`host/sdk-check.sh`](host/sdk-check.sh); output [`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

Options: such a documentation page; or another choice of yours.

## Proposals

<a id="p1"></a>
### P1. An API to suspend another thread and read its registers while its process runs

```
$ grep -rhoE "\bKn(Thread|Task)[A-Za-z]*(Suspend|Resume|Freeze|Unfreeze|Context)[A-Za-z]*\b" coresrv | sort -u
KnTaskGetThreadContext
KnTaskResume
KnThreadContext
KnThreadResumeByHandle
KnThreadSuspendCurrent
```

These are the APIs in the thread and task headers (`$SDK/sysroot-aarch64-kos/include/coresrv`) with Suspend, Resume,
Freeze or Context in their names. `KnThreadSuspendCurrent` "Suspends the calling thread", and `KnTaskGetThreadContext`
"Gets the context of a thread that is part of a frozen process" (`coresrv/thread/thread_api.h`,
`coresrv/task/task_api.h`). The POSIX route is closed, as the POSIX support limitations page ([web page](https://support.kaspersky.com/kos-community-edition/2.0/posix_uns_ifaces)) documents: for
`pthread_kill()`, "You cannot send a signal to a thread", and for `kill()`, "Only the `SIGTERM` signal can be sent."

**Motivation.** A garbage-collected runtime must stop its threads at safe points before it collects, and a debugger or
profiler must stop a thread to see where it is. .NET normally interrupts a thread running managed code with a signal
(`pthread_kill`) to bring it to a safe point quickly. On KasperskyOS my port has the compiler insert polls in loops
instead, which works but costs time in every loop.

**Security.** This stays within one process, whose threads can already read and write all its memory, and
`KnTaskGetThreadContext` already gets "the context of a thread that is part of a frozen process". And since "The Kaspersky Security
Module makes decisions regarding interaction between processes and the kernel the same way it makes decisions regarding
interaction between a process and other processes" (p. 46 of the 1.4 manual, [web page](https://support.kaspersky.com/kos-community-edition/1.4/overview_ipc_kernel); in the 2.0 manual, p. 59, [web page](https://support.kaspersky.com/kos-community-edition/2.0/overview_ipc_kernel): "The Kaspersky
Security Module controls interactions between processes and the kernel the same way it controls interactions between a
process and other processes."), a solution's policy could allow or deny such a call per program.

**On 2.0.0.179:** the same list of APIs.

Check: [`host/thread-api-check.sh`](host/thread-api-check.sh); output
[`results/thread-api-check.out`](results/thread-api-check.out), [`results/2.0.0.179/thread-api-check.out`](results/2.0.0.179/thread-api-check.out).

Is there, or is there planned, a way for one thread to stop another thread of the same process and read or change its
registers?

<a id="p2"></a>
### P2. A supported way for an exception handler to resume the thread at a new address with new registers

`KnTaskSetExceptionHandler` (`coresrv/task/task_api.h`) documents: "If the exception is successfully handled, this
function returns a value other than RTL_NULL." It does not say where the faulting thread then continues, or whether the
handler may change the registers that the thread continues with. The handler receives an `ExceptionInfo` (type, code,
fault address); the registers are available through `RtlGetLastTrapFrame()` (`thread/tcbpage.h`: "Copies last
exception Trap Frame to the specified address."), which neither manual mentions.

**Motivation.** Runtimes turn a null dereference or a stack overflow in managed code into an exception by resuming the
thread at a different address with changed registers (what `ucontext`-based signal handlers do on other systems). My
port does this today by restoring a changed copy of the trap frame itself from inside the handler, which depends on
undocumented details that a new SDK release may change.

**Security.** The handler belongs to the faulting thread's own process, and my port already resumes threads this way,
so a supported call adds no new power, and it could have the kernel check what is resumed (no privileged state, an
executable target address). Setting all registers from memory is the primitive behind sigreturn-oriented programming on
other systems, so such a check matters.

**On 2.0.0.179:** the same header lines.

Check: [`host/thread-api-check.sh`](host/thread-api-check.sh), which prints the quoted header lines and
`ExceptionInfo`'s declaration; output [`results/thread-api-check.out`](results/thread-api-check.out), [`results/2.0.0.179/thread-api-check.out`](results/2.0.0.179/thread-api-check.out).

Is there a supported way to resume at a new address with new registers, and could `ExceptionInfo`,
`RtlGetLastTrapFrame()` and the `HalTrapFrame` layout be documented?

<a id="p3"></a>
### P3. A way to query the CPU's features

On QEMU with `-cpu cortex-a57`, which has Advanced SIMD:

```
[check] sysctlbyname("hw.optional.AdvSIMD") -> -1, errno No such file or directory, value -1
[check] sysctlbyname("hw.optional.arm.AdvSIMD") -> -1, errno No such file or directory, value -1
[check] sysctlbyname("hw.optional.floatingpoint") -> -1, errno No such file or directory, value -1
[check] sysctlbyname("hw.optional.arm.FEAT_AES") -> -1, errno No such file or directory, value -1
[check] sysctlbyname("hw.ncpu") -> -1, errno Function not implemented, value -1
[check] done
```

There is no `sys/auxv.h` or `getauxval()` in the SDK either.

**On 2.0.0.179** (QEMU with `-cpu cortex-a55`): the same output and the same headers.

**Motivation.** A program compiled for more than the baseline instruction set must check at startup that the CPU has
it, and code with optional fast paths (for example the AES instructions) must learn whether it may take them. .NET
checks for the arm64 baseline (Advanced SIMD) at startup and refused to run until my port skipped the check on
KasperskyOS.

Check: [`src/cpu-check.c`](src/cpu-check.c), [`host/sdk-check.sh`](host/sdk-check.sh) (`sys/auxv.h`, `getauxval`); output
[`results/cpu.kos.out`](results/cpu.kos.out), [`results/2.0.0.179/cpu.kos.out`](results/2.0.0.179/cpu.kos.out), [`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

Options: have `getauxval(AT_HWCAP)` or the `hw.optional.*` sysctls report the CPU's features; document another supported
way to learn them; or another choice of yours.

## Portability notes

Behaviour POSIX requires, listed because Linux differs.

| Check (`net-check.c`) | KasperskyOS CE 1.4.0.102 | KasperskyOS CE 2.0.0.179 | Linux |
|---|---|---|---|
| `poll(513 entries, every fd -1, timeout 0)` | -1, errno Invalid argument | -1, errno Invalid argument | 0 |
| `poll(512 entries, every fd -1, timeout 0)` | 0 | 0 | 0 |

[`poll()`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/poll.html) "shall fail" with `EINVAL` when "The nfds argument is greater than {OPEN_MAX}", and the SDK's `limits.h`
defines `OPEN_MAX` 512. Code that polls more descriptors has to split the set.

Check: [`src/net-check.c`](src/net-check.c), [`host/sdk-check.sh`](host/sdk-check.sh) (`OPEN_MAX`); output
[`results/net.kos.out`](results/net.kos.out), [`results/2.0.0.179/net.kos.out`](results/2.0.0.179/net.kos.out), [`results/net.linux.out`](results/net.linux.out), [`results/sdk-check.out`](results/sdk-check.out), [`results/2.0.0.179/sdk-check.out`](results/2.0.0.179/sdk-check.out).

#!/bin/bash
# Checks of the SDK sysroot: OpenSSL headers, getentropy, functions declared but defined nowhere, fallocate flags,
# commonly probed headers. Each command is printed as "$ <command>" before its output.
#   host/sdk-check.sh [SDK directory]
export SDK=${1:-/opt/KasperskyOS-Community-Edition-Qemu-1.4.0.102}
cd "$SDK/sysroot-aarch64-kos" || exit 1
run() { echo "\$ $*"; bash -c "$*" 2>&1; echo; }

echo "# OpenSSL: libraries without headers"
run 'strings lib/libcrypto.a | grep -m1 "^OpenSSL 1"'
run 'grep -h ^Version lib/pkgconfig/openssl.pc'
run 'find . -name opensslv.h -o -type d -name openssl | wc -l'

echo "# getentropy: referenced by libcrypto, defined and declared nowhere"
run '$SDK/toolchain/bin/llvm-nm lib/libcrypto.a 2>/dev/null | grep -w getentropy'
run 'for a in lib/*.a; do $SDK/toolchain/bin/llvm-nm --defined-only $a 2>/dev/null | grep -qw -E "[TW] getentropy" && echo $a; done | wc -l'
run 'grep -rlw getentropy include --exclude-dir=boost | wc -l'

echo "# GSSAPI / Kerberos"
run 'find . \( -iname "*gssapi*" -o -iname "libkrb5*" \) | wc -l'

echo "# Declared but defined in no library"
run 'grep -nw -E "shm_open|setgrent|getgrent|endgrent" include/sys/mman.h include/grp.h'
run 'for f in shm_open setgrent getgrent endgrent; do n=0; for a in lib/*.a; do $SDK/toolchain/bin/llvm-nm --defined-only $a 2>/dev/null | grep -qw -E "[TW] $f" && n=$((n+1)); done; echo "$f: defined in $n archives"; done'

echo "# fallocate without FALLOC_FL_* flags"
run 'grep -nw fallocate include/fcntl.h'
run '$SDK/toolchain/bin/llvm-nm --defined-only lib/libc.a 2>/dev/null | grep -w -E "[TW] fallocate"'
run 'grep -rn FALLOC_FL_ include | wc -l'

echo "# Headers that portable code often probes"
run 'for h in sys/syscall.h sys/statfs.h sys/vfs.h endian.h elf.h malloc.h mntent.h sys/auxv.h sys/epoll.h sys/event.h sys/inotify.h linux/futex.h sys/endian.h sys/exec_elf.h; do [ -e include/$h ] && echo "present  $h" || echo "absent   $h"; done'
run 'grep -c _SC_AVPHYS_PAGES include/unistd.h'
run 'grep -h -E "PRODUCT_NAME|PRODUCT_VERSION" include/platform/version.h'

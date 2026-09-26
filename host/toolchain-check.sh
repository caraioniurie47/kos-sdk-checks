#!/bin/bash
# Checks of the SDK toolchain: -static-pie, unprefixed compilers, where the SDK says how stdout reaches the console,
# typos in log messages. Each command is printed as "$ <command>" before its output.
#   host/toolchain-check.sh [SDK directory]
export SDK=${1:-/opt/KasperskyOS-Community-Edition-Qemu-1.4.0.102}
W=$(mktemp -d) || exit 1
trap 'rm -rf "$W"' EXIT
cd "$W" || exit 1
printf 'int main(void) { return 0; }\n' > t.c
run() { echo "\$ $*"; bash -c "$*" 2>&1; echo; }

echo "# -static-pie"
run '$SDK/toolchain/bin/aarch64-kos-clang -static-pie t.c -o t-static-pie'
run 'file t-static-pie | sed "s|.*: ||"'
run '$SDK/toolchain/bin/aarch64-kos-clang -static t.c -o t-static'
run 'file t-static | sed "s|.*: ||"'

echo "# unprefixed compilers in toolchain/bin"
run 'ls $SDK/toolchain/bin | grep -E "^clang(-[0-9]+)?$|^clang\+\+$"'
run '$SDK/toolchain/bin/clang-17 --version | head -2'

echo "# stdout without a VFS program: where the SDK says it"
run 'grep -rn -i "stdout\|stderr" $SDK/examples/hello/hello/src/hello.c | head -5'

echo "# typos in log messages"
run 'strings $SDK/sysroot-aarch64-kos/lib/libvfs_remote.a | grep -m1 connetion'
run 'strings $SDK/sysroot-aarch64-kos/lib/libem_transport_lib.a | grep -m1 succesfully'

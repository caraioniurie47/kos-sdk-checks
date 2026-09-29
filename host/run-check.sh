#!/bin/bash
# Build src/<check>-check.c into a KasperskyOS image, boot it in QEMU, and print the program's "[check]" lines.
# QEMU is stopped once the program prints "[check] done", faults, is ended for lack of memory, or after 10 minutes.
#   host/run-check.sh <net|net2|net3|fs|mem|sys|oom|uname|cpu|sig> [SDK directory]
# BUILD_DIR overrides the build directory (default: build-<check> in the repository); the QEMU console goes to
# qemu.log in it.
set -u
CHECK=${1:?usage: run-check.sh <net|net2|net3|fs|mem|sys|oom|uname|cpu|sig> [SDK directory]}
SDK=${2:-/opt/KasperskyOS-Community-Edition-Qemu-1.4.0.102}
REPO=$(cd "$(dirname "$0")/.." && pwd)
BUILD=${BUILD_DIR:-$REPO/build-$CHECK}
LOG=$BUILD/qemu.log

"$SDK"/toolchain/bin/cmake -S "$REPO" -B "$BUILD" \
    -D CMAKE_TOOLCHAIN_FILE="$SDK"/toolchain/share/toolchain-aarch64-kos.cmake -D CHECK="$CHECK" > /dev/null || exit 1
if ! "$SDK"/toolchain/bin/cmake --build "$BUILD" --target kos-qemu-image > "$BUILD/build.log" 2>&1; then
    tail -20 "$BUILD/build.log"
    exit 1
fi

: > "$LOG"
# Its own process group, so that stopping it stops only this QEMU.
setsid "$SDK"/toolchain/bin/cmake --build "$BUILD" --target sim > "$LOG" 2>&1 < /dev/null &
pid=$!
for _ in $(seq 1 600); do
    grep -aqF "[check] done" "$LOG" && break
    grep -aq -E "Unhandled Page Fault|Unhandled Overcommit|Terminating task" "$LOG" && break
    kill -0 "$pid" 2>/dev/null || break
    sleep 1
done
sleep 2
kill -- -"$pid" 2>/dev/null
wait "$pid" 2>/dev/null

grep -a -F -e "[check]" -e "Unhandled Page Fault" -e "Unhandled Overcommit" -e "Terminating task" "$LOG" | tr -d '\r'
grep -aqF "[check] done" "$LOG"

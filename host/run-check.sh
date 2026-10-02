#!/bin/bash
# Build src/<check>-check.c into a KasperskyOS image, boot it in QEMU, and print the program's "[check]" lines.
# QEMU is stopped once the program prints "[check] done", faults, is ended for lack of memory, or after 10 minutes;
# for novfs also 15 s after its C runtime falls back to the VFS stub.
#   host/run-check.sh <net|net2|net3|fs|mem|sys|oom|uname|cpu|sig|novfs> [SDK directory]
# BUILD_DIR overrides the build directory (default: build-<check> in the repository); the QEMU console goes to
# qemu.log in it.
set -u
CHECK=${1:?usage: run-check.sh <net|net2|net3|fs|mem|sys|oom|uname|cpu|sig|novfs> [SDK directory]}
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
stub=0
for _ in $(seq 1 600); do
    grep -aqF "[check] done" "$LOG" && break
    grep -aq -E "Unhandled Page Fault|Unhandled Overcommit|Terminating task" "$LOG" && break
    kill -0 "$pid" 2>/dev/null || break
    # novfs: should its output not arrive, stop 15 s after its runtime falls back to the stub.
    # (Einit's and DCM's runtimes log the same fallback at boot in every image; only checks.Check's line counts.)
    [ "$CHECK" = novfs ] && grep -aq "checks\.Check.*initialized with stub" "$LOG" && stub=$((stub + 1)) &&
        [ "$stub" -ge 15 ] && break
    sleep 1
done
sleep 2
kill -- -"$pid" 2>/dev/null
wait "$pid" 2>/dev/null

# novfs: also the C runtime's and the VFS client's log lines, whose timestamps show the wait.
VFSLOG='\[check\]'
[ "$CHECK" = novfs ] && VFSLOG='\]\[(CRT0|VFS_CLIENT|VFS_INIT)\]'
grep -a -E -e "\[check\]|Unhandled Page Fault|Unhandled Overcommit|Terminating task" -e "$VFSLOG" "$LOG" | tr -d '\r'
grep -aqF "[check] done" "$LOG"

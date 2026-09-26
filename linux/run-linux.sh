#!/bin/bash
# Run a check natively on Linux for comparison: net, fs and sig only (mem, uname and cpu use KasperskyOS interfaces).
# kos_net.h here stubs out the KasperskyOS network setup. Needs root, for a private tmpfs /tmp.
#   sudo linux/run-linux.sh <net|fs|sig>
set -u
CHECK=${1:?usage: run-linux.sh <net|fs|sig>}
DIR=$(cd "$(dirname "$0")" && pwd)
SRC=$DIR/../src/$CHECK-check.c
# Not under /tmp, which the tmpfs below hides.
W=$(mktemp -d -p /var/tmp) || exit 1
trap 'rm -rf "$W"' EXIT
echo "\$ uname -sr; id -u"; uname -sr; id -u
gcc -D_GNU_SOURCE -I"$DIR" -o "$W/check" "$SRC" -lpthread || exit 1
# /tmp is a private 64 MiB tmpfs for the run: fs-check.c asks posix_fallocate for 1 TiB, which on a disk-backed /tmp
# allocates until the disk is full before failing with ENOSPC.
unshare -m sh -c 'mount -t tmpfs -o size=64m tmpfs /tmp && echo "\$ findmnt -no FSTYPE,OPTIONS /tmp" &&
    findmnt -no FSTYPE,OPTIONS /tmp && "$0"' "$W/check"

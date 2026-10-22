#!/bin/bash
# SDK 2.0: configure and build this repository's 1.4 image project (the root CMakeLists.txt, which calls
# build_kos_qemu_image()) with the given SDK, and show what the SDK has instead. Each command is printed as
# "$ <command>" before its output; it runs in a temporary copy of the project.
#   host/image-check.sh [SDK directory]
export SDK=${1:-/opt/KasperskyOS-Community-Edition-Qemu-arm64-2.0.0.179}
REPO=$(cd "$(dirname "$0")/.." && pwd)
W=$(mktemp -d) || exit 1
trap 'rm -rf "$W"' EXIT
for d in CMakeLists.txt einit src resources; do
    cp -r "$REPO/$d" "$W/" || exit 1
done
cd "$W" || exit 1
run() { echo "\$ $*"; bash -c "$*" 2>&1; echo; }

run '$SDK/toolchain/bin/cmake -B build -D CMAKE_TOOLCHAIN_FILE=$SDK/toolchain/share/toolchain-aarch64-kos.cmake -D CHECK=uname > configure.log 2>&1; echo "exit code $?"'
run 'grep -B1 -A4 "be added" configure.log | sed "s|$SDK|\$SDK|g"'
run '$SDK/toolchain/bin/cmake --build build --target kos-qemu-image > build.log 2>&1; echo "exit code $?"'
run 'grep -m1 "No rule" build.log'
run 'ls -A $SDK/libexec | wc -l'
run 'grep -rln "^ *function *(build_kos_qemu_image" $SDK --include=*.cmake | sed "s|$SDK|\$SDK|"'
run 'grep -rl build_kos_qemu_image $SDK/examples --include=CMakeLists.txt | wc -l'
run 'grep -rl generate_qemu_target $SDK/examples --include=CMakeLists.txt | wc -l'

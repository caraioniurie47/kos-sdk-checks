#!/bin/bash
# Which thread and task APIs the SDK headers declare for suspending a thread or reading its registers, and what the
# exception handler API declares. Each command is printed as "$ <command>" before its output.
#   host/thread-api-check.sh [SDK directory]
export SDK=${1:-/opt/KasperskyOS-Community-Edition-Qemu-1.4.0.102}
cd "$SDK/sysroot-aarch64-kos/include" || exit 1
run() { echo "\$ $*"; bash -c "$*" 2>&1; echo; }
run 'grep -rhoE "\bKn(Thread|Task)[A-Za-z]*(Suspend|Resume|Freeze|Unfreeze|Context)[A-Za-z]*\b" coresrv | sort -u'
run 'grep -rn -B3 "KnTaskGetThreadContext" coresrv | grep -v "^--$" | head -20'
run 'grep -rn "KnTaskSetExceptionHandler" coresrv | head -5'
run 'grep -rhn "pthread_kill\|pthread_suspend\|pthread_getcontext" pthread.h signal.h | head'

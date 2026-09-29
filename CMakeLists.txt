# Checks of KasperskyOS Community Edition 1.4.0.102 (QEMU, aarch64) behaviour against POSIX and Linux.
# One program, checks.Check, built from src/<CHECK>-check.c, with the SDK's prebuilt VfsRamFs as its file system and
# VfsNet as its network stack. Build and run with the SDK's cmake:
#   $SDK/toolchain/bin/cmake -B build-net -D CMAKE_TOOLCHAIN_FILE=$SDK/toolchain/share/toolchain-aarch64-kos.cmake -D CHECK=net
#   $SDK/toolchain/bin/cmake --build build-net --target sim
# host/run-check.sh does both and stops QEMU once the program prints "[check] done".

cmake_minimum_required (VERSION 3.25)

project (kos_sdk_checks)

include (platform)
initialize_platform (FORCE_STATIC)
project_header_default ("STANDARD_GNU_17:YES" "EXTRA_STRICT_WARNINGS:YES")
include (platform/nk)
include (platform/image)

find_package (precompiled_vfs REQUIRED)
find_package (vfs REQUIRED)

set (CHECKS net fs mem uname cpu sig net2 net3 sys oom)
set (CHECK "net" CACHE STRING "Which check to build: one of ${CHECKS}")
if (NOT CHECK IN_LIST CHECKS)
    message (FATAL_ERROR "CHECK must be one of ${CHECKS} (got '${CHECK}')")
endif ()

set (NK_RESOURCES ${CMAKE_SOURCE_DIR}/resources/xdl)
nk_include_directories (${NK_RESOURCES})
nk_build_edl_files (check_edl_files
                    NK_MODULE "checks"
                    EDL       "${NK_RESOURCES}/checks/Check.edl")

add_executable (Check "src/${CHECK}-check.c")
add_dependencies (Check check_edl_files)
target_link_libraries (Check vfs::client)
# vfs::client would add the SDK's default VFS program; VfsRamFs and VfsNet serve Check instead (init.yaml.in).
set_target_properties (Check PROPERTIES vfs::entity_REPLACEMENT "")

build_kos_qemu_image (kos-qemu-image
                      EINIT_ENTITY EinitQemu
                      CONNECTIONS_CFG "einit/init.yaml.in"
                      SECURITY_PSL "einit/security.psl"
                      IMAGE_FILES Check precompiled_vfs::VfsRamFs precompiled_vfs::VfsNet)

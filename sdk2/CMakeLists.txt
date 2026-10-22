# The same checks on KasperskyOS Community Edition 2.0.0.179 (QEMU, aarch64). SDK 2.0 builds no Einit-only image: a
# prebuilt MinimalKOS boots, programs are KPA packages started by ExecutionManager, and system programs a solution adds
# run from a product ROMFS. Here checks.Check is the KPA package Check, started at boot from system_services.json;
# VfsRamFs, VfsNet and the dnet driver VfsNet needs run from the product ROMFS with the SDK's default environment and
# arguments, as in the 1.4 image (CHECK=novfs: no product ROMFS, and Check has no VFS connection). Build and run with
# the SDK's cmake:
#   $SDK/toolchain/bin/cmake -S sdk2 -B build-net -D CMAKE_TOOLCHAIN_FILE=$SDK/toolchain/share/toolchain-aarch64-kos.cmake -D CHECK=net
#   $SDK/toolchain/bin/cmake --build build-net --target sim
# host/run-check.sh picks this directory for an SDK with MinimalKOS.

cmake_minimum_required (VERSION 3.29)

project (kos_sdk_checks)

include (platform)
initialize_platform (FORCE_STATIC)
project_header_default ("STANDARD_GNU_17:YES" "EXTRA_STRICT_WARNINGS:YES")
include (platform/nk)
include (platform/options)
include (platform/kpa)

find_package (minimal_kos REQUIRED)
find_package (vfs REQUIRED)
find_package (precompiled_vfs REQUIRED)
find_package (dnet REQUIRED)
include (${KL_SDK_ROOT_PATH}/share/vendor_rom/build_vendor_rom.cmake)

set (CHECKS net fs mem uname cpu sig net2 net3 sys oom novfs falloc falloc4g pollclose)
set (CHECK "net" CACHE STRING "Which check to build: one of ${CHECKS}")
if (NOT CHECK IN_LIST CHECKS)
    message (FATAL_ERROR "CHECK must be one of ${CHECKS} (got '${CHECK}')")
endif ()

set (REPO ${CMAKE_SOURCE_DIR}/..)
set (NK_RESOURCES ${REPO}/resources/xdl)
nk_include_directories (${NK_RESOURCES})
nk_build_edl_files (check_edl_files
                    NK_MODULE "checks"
                    EDL       "${NK_RESOURCES}/checks/Check.edl")

add_executable (Check "${REPO}/src/${CHECK}-check.c")
add_dependencies (Check check_edl_files)
target_link_libraries (Check vfs::client)
set_target_properties (Check PROPERTIES vfs::entity_REPLACEMENT "")

add_kpa_package (Check.kpa ID Check MANIFEST_V "2")
add_kpa_component (Check.kpa RESOURCE Check TYPE "bin")
if (CHECK STREQUAL "novfs")
    # The nearest 2.0 has to "no VFS program": MinimalKOS always runs its own (kl.VfsEntity), so Check gets no VFS
    # connection and no backend setting instead.
    add_kpa_component (Check.kpa
                       RUN_CONFIGURATION "default"
                       TYPE              "application"
                       NAME              "Check"
                       EIID              "checks.Check"
                       PATH              "bin/Check"
                       NO_AUTO_VFS_CONNECTION)
else ()
    # The client form that waits for the server, as the SDK examples use for programs started at boot.
    add_kpa_component (Check.kpa
                       RUN_CONFIGURATION "default"
                       TYPE              "application"
                       NAME              "Check"
                       EIID              "checks.Check"
                       PATH              "bin/Check"
                       ENV               "VFS_FILESYSTEM_BACKEND=client:task:kl.VfsRamFs:timeout_dcm_ms:600000"
                                         "VFS_NETWORK_BACKEND=client:task:kl.VfsNet:timeout_dcm_ms:600000"
                       NO_AUTO_VFS_CONNECTION)
endif ()

set (EINIT_SRC ${CMAKE_SOURCE_DIR}/einit)
set (ROOTFS_DIR ${CMAKE_BINARY_DIR}/rootfs_img)
find_program (TAR_COMMAND tar REQUIRED)

if (CHECK STREQUAL "novfs")
    build_vendor_romfs (vendor_romfs
                        SECURITY_PSL_FILE ${EINIT_SRC}/security-novfs.psl
                        EDL_TARGETS       check_edl_files)
    add_custom_target (rootfs_part_src
                       COMMAND ${CMAKE_COMMAND} -E make_directory ${ROOTFS_DIR}
                       COMMAND ${TAR_COMMAND} --extract --keep-directory-symlink
                                              --file ${MINIMAL_KOS_ROOTFS_ARCHIVE} --directory ${ROOTFS_DIR}
                       COMMAND ${CMAKE_COMMAND} -E copy ${EINIT_SRC}/novfs/initial_install.json
                                                        ${EINIT_SRC}/novfs/system_services.json
                                                        ${ROOTFS_DIR}${MINIMAL_KOS_INITIAL_SETTINGS_DIR}/
                       COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_PROPERTY:Check.kpa,KPA_OUTPUT_DIR>/Check.kpa
                                                        ${ROOTFS_DIR}${MINIMAL_KOS_INITIAL_PACKAGES_DIR}
                       DEPENDS Check.kpa)
else ()
    set (PRODUCT_ROM_PROGRAMS dnet_executables::whole precompiled_vfs::VfsNet precompiled_vfs::VfsRamFs)
    set (PRODUCT_INIT_YAML "${CMAKE_BINARY_DIR}/product_init.yaml")
    configure_file (${EINIT_SRC}/product_init.yaml.in "${PRODUCT_INIT_YAML}")

    build_vendor_romfs (vendor_romfs
                        WITH_PRODUCT_ROMFS
                        SECURITY_PSL_FILE ${EINIT_SRC}/security.psl
                        EDL_TARGETS       check_edl_files)
    build_einit (product_einit ${PRODUCT_INIT_YAML} DEPENDS ${PRODUCT_ROM_PROGRAMS})
    build_romfs (product_romfs product_einit FILES ${PRODUCT_ROM_PROGRAMS})
    add_custom_target (rootfs_part_src
                       COMMAND ${CMAKE_COMMAND} -E make_directory ${ROOTFS_DIR}
                       COMMAND ${TAR_COMMAND} --extract --keep-directory-symlink
                                              --file ${MINIMAL_KOS_ROOTFS_ARCHIVE} --directory ${ROOTFS_DIR}
                       COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_PROPERTY:product_romfs,ROMFS_LOCATION>
                                                        ${ROOTFS_DIR}/product_romfs
                       COMMAND ${CMAKE_COMMAND} -E copy ${EINIT_SRC}/initial_install.json
                                                        ${EINIT_SRC}/system_services.json
                                                        ${ROOTFS_DIR}${MINIMAL_KOS_INITIAL_SETTINGS_DIR}/
                       COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_PROPERTY:Check.kpa,KPA_OUTPUT_DIR>/Check.kpa
                                                        ${ROOTFS_DIR}${MINIMAL_KOS_INITIAL_PACKAGES_DIR}
                       DEPENDS product_romfs Check.kpa)
endif ()

build_rootfs (rootfs DIR ${ROOTFS_DIR} DEPENDS rootfs_part_src)

# Kernel startup parameters, e.g. vmm.overcommit=disabled (manual, "Kernel startup parameters"); empty by default.
set (KOS_KERNEL_CMDLINE "" CACHE STRING "Kernel startup parameters passed with QEMU's -append")
set (QEMU_CMDLINE_ARGS)
if (KOS_KERNEL_CMDLINE)
    set (QEMU_CMDLINE_ARGS KERNEL_CMDLINE ${KOS_KERNEL_CMDLINE})
endif ()
generate_qemu_target (qemu
                      KERNEL_IMAGE ${MINIMAL_KOS_IMAGE}
                      VENDOR_IMAGE vendor_romfs
                      ROOTFS_IMAGE rootfs
                      ${QEMU_CMDLINE_ARGS})

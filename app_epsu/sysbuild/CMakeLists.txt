# SPDX-License-Identifier: Apache-2.0

# MCUboot expects the signing key relative to its own source tree.
# Create a symlink so it finds our key without hardcoding absolute paths.
get_filename_component(OUR_KEY
  ${CMAKE_CURRENT_SOURCE_DIR}/../../boot/keys/imgtool_key.pem
  ABSOLUTE)
get_filename_component(BOOT_DIR
  $ENV{ZEPHYR_BASE}/../bootloader/boot
  ABSOLUTE)
file(MAKE_DIRECTORY ${BOOT_DIR}/keys)
execute_process(
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${OUR_KEY}
    ${BOOT_DIR}/keys/imgtool_key.pem
  ERROR_QUIET)

find_package(Sysbuild REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sysbuild LANGUAGES)

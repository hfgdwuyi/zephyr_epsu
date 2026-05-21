get_filename_component(MCUBOOT_APP_DIR
  $ENV{ZEPHYR_BASE}/../bootloader/mcuboot/boot/zephyr
  ABSOLUTE)
get_filename_component(OUR_KEY
  ${CMAKE_CURRENT_SOURCE_DIR}/../../src/application/boot_keys/imgtool_key.pem
  ABSOLUTE)
file(MAKE_DIRECTORY ${MCUBOOT_APP_DIR}/boot_keys)
execute_process(
  COMMAND ${CMAKE_COMMAND} -E create_symlink ${OUR_KEY}
    ${MCUBOOT_APP_DIR}/boot_keys/imgtool_key.pem
  ERROR_QUIET)
find_package(Sysbuild REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sysbuild LANGUAGES)

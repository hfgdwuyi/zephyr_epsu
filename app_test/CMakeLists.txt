cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app_test)

target_sources(app PRIVATE
    src/main.c
    src/ota_update.c
)

# Generate iterable section boundary symbols for OTA HTTP service resources
zephyr_linker_sources(SECTIONS sections-rom.ld)

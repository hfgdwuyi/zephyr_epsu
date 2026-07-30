# CiosZhong — sysbuild (no MCUboot, no extra images)
find_package(Sysbuild REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sysbuild LANGUAGES)

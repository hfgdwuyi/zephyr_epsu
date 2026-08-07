#!/bin/sh
#
# flash.sh — reliable one-shot flash for CiosZhong PSU (NUCLEO-H745ZI-Q)
#
# Why connect-under-reset is required:
#   The running firmware's watchdogs (internal WWDG + external MAX6703A) reset
#   the chip in a loop, and the M4 core has no firmware so it lockups. A plain
#   "connect then halt" races against those resets and often fails with
#   "Target not examined yet". Holding NRST during connect/examine and halting
#   before the firmware runs makes flashing deterministic.
#
# Requires: openocd with board/st_nucleo_h745zi.cfg
#           (homebrew: brew install openocd)
#
# Usage:
#   ./flash.sh                 # flash ../build/zephyr/zephyr.hex
#   BUILD_DIR=... ./flash.sh   # flash a different build directory
#   HEX=... ./flash.sh         # flash a specific image file
#
# west alternative (writes fine, but the runner's extra reset-halt between
# write and verify produces lockup noise instead of a clean "Verified OK"):
#   cd /Users/mac/project/02_zephyr/zephyrproject
#   /Users/mac/project/03_siemens/ciosZhong_ePSU/.venv/bin/python -m west flash \
#     -d /Users/mac/project/03_siemens/ciosZhong_ePSU/build -r openocd \
#     --cmd-pre-init "reset_config srst_only connect_assert_srst" \
#     --cmd-pre-init "adapter speed 1000" \
#     --cmd-reset-halt "reset halt"

set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR=${BUILD_DIR:-"$(dirname "$SCRIPT_DIR")/build"}
HEX=${HEX:-"$BUILD_DIR/zephyr/zephyr.hex"}

command -v openocd >/dev/null 2>&1 || { echo "error: openocd not found in PATH" >&2; exit 1; }
[ -f "$HEX" ] || { echo "error: $HEX not found (run west build first)" >&2; exit 1; }

echo "Flashing $HEX ..."
openocd \
    -f board/st_nucleo_h745zi.cfg \
    -c "reset_config srst_only connect_assert_srst" \
    -c "adapter speed 1000" \
    -c "init" \
    -c "reset halt" \
    -c "program $HEX verify reset exit"

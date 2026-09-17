#!/bin/sh
# flash_stlink.sh - flash the product board (STM32H745)
#
# NRST is wired, so openocd with a hardware reset is the preferred path.
# If the first attempt fails (ST-Link / watchdog timing), it retries the same
# .hex, which carries its own address (vector table at 0x08000000).
#
# Important: st-flash only accepts raw .bin, which has no address and may be a
# MCUboot slot0 image (empty header in the first 0x400 bytes) - flashing it at
# 0x08000000 "succeeds" but does not boot. This script uses openocd + .hex.
#
# Usage:
#   ./flash_stlink.sh                     # flash build/zephyr/zephyr.hex
#   ./flash_stlink.sh <path-to.hex>       # flash the given hex
#
# Requires openocd with board/st_nucleo_h745zi.cfg

set -e
cd "$(dirname "$0")"

HEX=${1:-build/zephyr/zephyr.hex}
[ -f "$HEX" ] || { echo "error: $HEX not found (run west build first)"; exit 1; }

echo "Flashing $HEX ..."

for i in 1 2 3 4 5 6; do
    echo "=== attempt $i ==="
    if openocd -f board/st_nucleo_h745zi.cfg \
                -c "adapter speed 950" \
        -c "init" -c "reset halt" -c "halt" \
        -c "program $HEX verify reset exit" \
        2>&1 | tee /tmp/flash_openocd.log | grep -q "Verified OK"; then
        echo ">>> SUCCESS (openocd)"
        exit 0
    fi
    sleep 2
done

echo "error: flash failed after 6 attempts"
echo "Hint: on repeated failure, unplug the ST-Link for >= 10 s; if needed pull BOOT0 high and power-cycle."
exit 1

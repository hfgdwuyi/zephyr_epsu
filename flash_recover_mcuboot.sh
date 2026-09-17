#!/bin/sh
# flash_recover_mcuboot.sh - flash MCUboot + App (first time / recovery, ST-Link)
#
# Sample firmware combination:
#   MCUboot bootloader  v1.0.0     (verified on the board)
#   App firmware        v0.2.x     (serial DFU + MCUboot confirm)
#
# Layout (internal flash0, 2 MB from 0x08000000):
#   boot    MCUboot        build-mcuboot/zephyr/zephyr.bin   @ 0x08000000
#   app     App (slot0)    build/zephyr/zephyr.signed.bin    @ 0x08020000
# Later customer upgrades use serial DFU (psu_dfu.py -> slot1, MCUboot).
#
# Usage:
#   ./flash_recover_mcuboot.sh            # boot + app
#   ./flash_recover_mcuboot.sh boot       # boot only
#   ./flash_recover_mcuboot.sh app        # app only
#
# Notes:
#   - Flashes through the NRST hardware-reset window, retrying 6 times.
#   - The running firmware / MAX6703A watchdog can disturb SWD: if it keeps
#     failing, pull BOOT0 high, power off >= 10 s, power on and rerun.
set -u
cd "$(dirname "$0")"

BOOT=build-mcuboot/zephyr/zephyr.bin
APP=build/zephyr/zephyr.signed.bin

WANT=${1:-all}   # all | boot | app

flash_once() {   # $1=bin  $2=addr  $3=name
    echo "== flashing $3 ($1) @ $2 =="
    openocd -f board/st_nucleo_h745zi.cfg \
                -c "adapter speed 950" \
        -c "init" -c "reset halt" -c "halt" \
        -c "program $1 $2 verify reset exit" \
        2>&1 | tee /tmp/flash_$3.log | grep -q "Verified OK"
}

flash_retry() {  # $1=bin  $2=addr  $3=name
    i=1
    while [ "$i" -le 6 ]; do
        echo "=== attempt $i ($3) ==="
        if flash_once "$1" "$2" "$3"; then
            echo ">>> $3 flashed OK (verified)"
            return 0
        fi
        i=$((i + 1))
        sleep 2
    done
    echo "!!! $3 FAILED after 6 attempts"
    echo "    Try: pull BOOT0 high -> power off >= 10 s -> power on -> rerun"
    return 1
}

[ "$WANT" = "all" ] && [ ! -f "$BOOT" ] && { echo "error: $BOOT not found"; exit 1; }
[ "$WANT" = "all" ] && [ ! -f "$APP" ]  && { echo "error: $APP not found"; exit 1; }
[ "$WANT" = "boot" ] && [ ! -f "$BOOT" ] && { echo "error: $BOOT not found"; exit 1; }
[ "$WANT" = "app" ]  && [ ! -f "$APP" ]  && { echo "error: $APP not found"; exit 1; }

rc=0
case "$WANT" in
    all)
        flash_retry "$BOOT" 0x08000000 boot || rc=1
        flash_retry "$APP"  0x08020000 app  || rc=1
        ;;
    boot)
        flash_retry "$BOOT" 0x08000000 boot || rc=1
        ;;
    app)
        flash_retry "$APP" 0x08020000 app || rc=1
        ;;
    *)
        echo "usage: $0 [all|boot|app]"; exit 2
        ;;
esac

echo "done."
[ "$rc" -eq 0 ] && echo ">>> All images flashed. Pull BOOT0 low and power-cycle."
exit $rc

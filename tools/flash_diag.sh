#!/bin/sh
# flash_diag.sh - STM32H745 SWD connectivity diagnostics
#
# Usage:
#   ./tools/flash_diag.sh            # run all checks
#   ./tools/flash_diag.sh quick      # primary strategy only (fastest)
#
# Prints PASS/FAIL per strategy and a conclusion:
#   all FAIL but voltage OK  -> chip side (RDP lock / firmware owns SWD / bad solder)
#   only connects with BOOT0 high -> firmware reuses PA13/14 as GPIO
#   no voltage at all        -> SWD power / wiring problem
set -u
cd "$(dirname "$0")/.."

LOG=/tmp/flash_diag.log
: > "$LOG"

pass=0
fail=0

try() {   # $1=strategy name, rest = openocd args
    name=$1; shift
    if openocd "$@" 2>&1 | tee -a "$LOG" | grep -qE "target voltage|Target voltage"; then
        echo "PASS"
    else
        echo "FAIL"
    fi
}

echo "====  ST-Link probe detection ===="
if openocd -f interface/stlink-dap.cfg -c "transport select dapdirect_swd" \
        -c "adapter speed 100" -c "init" -c "shutdown" \
        2>&1 | tee -a "$LOG" | grep -q "STLINK V2\|STLINK-V3\|STLINK V3"; then
    echo "[1] probe detect: PASS"
    pass=$((pass+1))
else
    echo "[1] probe detect: FAIL (check USB / driver)"
    fail=$((fail+1))
fi

echo
echo "====  SWD connect strategies ===="
v=$(grep -oE "Target voltage: [0-9.]+" "$LOG" | head -1)
echo "    target voltage: ${v:-N/A}"

echo "[2] dapdirect_swd init:          \c"
if openocd -f interface/stlink-dap.cfg -c "transport select dapdirect_swd" \
        -c "adapter speed 400" -f target/stm32h7x.cfg -c "set DUAL_CORE 0" \
        -c "init" -c "halt" -c "shutdown" \
        2>&1 | tee -a "$LOG" | grep -qE "halted|target halted"; then
    echo "PASS"; pass=$((pass+1))
else
    echo "FAIL"; fail=$((fail+1))
fi

echo "[3] hla_swd init:                \c"
if openocd -f interface/stlink.cfg -c "transport select hla_swd" \
        -c "adapter speed 400" -f target/stm32h7x.cfg -c "set DUAL_CORE 0" \
        -c "init" -c "halt" -c "shutdown" \
        2>&1 | tee -a "$LOG" | grep -qE "halted|target halted"; then
    echo "PASS"; pass=$((pass+1))
else
    echo "FAIL"; fail=$((fail+1))
fi

echo "[4] dapdirect_swd 100kHz:        \c"
if openocd -f interface/stlink-dap.cfg -c "transport select dapdirect_swd" \
        -c "adapter speed 100" -f target/stm32h7x.cfg -c "set DUAL_CORE 0" \
        -c "init" -c "shutdown" \
        2>&1 | tee -a "$LOG" | grep -qE "Info :.*(idcode|ap\[|halted)"; then
    echo "PASS"; pass=$((pass+1))
else
    echo "FAIL"; fail=$((fail+1))
fi

echo "[5] connect_assert_srst:         \c"
if openocd -f interface/stlink-dap.cfg -c "transport select dapdirect_swd" \
        -c "adapter speed 400" -c "reset_config srst_only connect_assert_srst" \
        -f target/stm32h7x.cfg -c "set DUAL_CORE 0" \
        -c "init" -c "shutdown" \
        2>&1 | tee -a "$LOG" | grep -qE "halted|Info :.*idcode"; then
    echo "PASS"; pass=$((pass+1))
else
    echo "FAIL"; fail=$((fail+1))
fi

echo
echo "====  result: connect PASS=$((pass-1)) FAIL=$fail (probe detect excluded) ===="
echo
if [ "$pass" -ge 2 ]; then
    echo ">>> At least one strategy connected. Flash with flash_recover_mcuboot.sh / flash_stlink.sh"
    echo "    If it only connects with BOOT0 high, the firmware owns PA13/14:"
    echo "    erase / flash the bootloader first, or check the GPIO config."
elif grep -q "Target voltage:" "$LOG"; then
    echo ">>> All FAIL but target voltage OK -> chip side:"
    echo "    1) RDP level 1/2 (level 2 is permanent, chip must be replaced)"
    echo "    2) bad solder / damaged chip, or not an H745"
    echo "    3) SWDIO/SWCLK not connected (NRST high does not mean SWD works)"
    echo "    Try BOOT0 high once more; if it still fails check RDP / the chip."
else
    echo ">>> All FAIL and no target voltage -> wiring / power problem:"
    echo "    Check SWDIO/SWCLK/GND/3V3, the ST-Link USB and target power."
fi
echo
echo "Full log: $LOG"

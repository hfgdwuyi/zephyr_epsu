#!/bin/sh
# flash_force.sh - forced flashing under reset loops / watchdog interference
#
# Use when the MAX6703A keeps resetting the MCU (so a normal single openocd
# command cannot even halt the target: "timed out while waiting for target
# halted"). Rotates several connect/reset strategies with fast retries.
#
# Usage:
#   ./flash_force.sh              # flash boot + app
#   ./flash_force.sh app          # app only
#   ./flash_force.sh boot         # boot only
#   ./flash_force.sh app 40       # retries per strategy (default 20)
#
# If everything fails the reset is continuous, not a gap; fix it in hardware:
#   1) pull BOOT0 high + power off >= 10 s + power on
#   2) disconnect MAX6703A RST from MCU NRST
set -u
cd "$(dirname "$0")"

BOOT=build-mcuboot/zephyr/zephyr.bin
APP=build/zephyr/zephyr.signed.bin
WANT=${1:-all}
TRIES=${2:-20}

case "$WANT" in
    all)  TARGETS="$BOOT@0x08000000 $APP@0x08020000" ;;
    boot) TARGETS="$BOOT@0x08000000" ;;
    app)  TARGETS="$APP@0x08020000" ;;
    *) echo "usage: $0 [all|boot|app] [tries]"; exit 2 ;;
esac

# Clean up any leftover openocd (it holds the ST-Link and causes odd failures)
pkill -9 openocd 2>/dev/null
sleep 1

flash_one() {   # $1=file $2=addr $3=strategy $4=speed $5..=extra openocd args
    f=$1; a=$2; strat=$3; spd=$4; shift 4
    openocd -f board/st_nucleo_h745zi.cfg \
        -c "adapter speed $spd" "$@" \
        -c "init" -c "reset halt" \
        -c "program $f $a verify" \
        -c "shutdown" 2>&1
}

try_target() {  # $1=file $2=addr
    f=$1; a=$2
    base=$(basename "$f")

    for s in $(seq 1 "$TRIES"); do
        # Rotate 4 strategies: standard / fast / no-NRST / software reset
        case $((s % 4)) in
            1) set -- 950  ;;
            2) set -- 4000 ;;
            3) set -- 950 -c "reset_config none" ;;
            0) set -- 950 -c "cortex_m reset_config sysresetreq" ;;
        esac

        printf "\r  try %2d/%d (strategy %s) ... " "$s" "$TRIES" "$(( (s % 4) + 1 ))"
        out=$(flash_one "$f" "$a" "$s" "$@" 2>&1)
        if echo "$out" | grep -q "Verified OK"; then
            printf "\n>>> %s flashed OK (attempt %d)\n" "$base" "$s"
            return 0
        fi
        sleep 0.3
    done
    printf "\n!!! %s FAILED after %d attempts\n" "$base" "$TRIES"
    return 1
}

rc=0
for t in $TARGETS; do
    file=${t%@*}
    addr=${t#*@}
    echo "== flashing $file @ $addr =="
    try_target "$file" "$addr" || rc=1
done

echo
if [ "$rc" -eq 0 ]; then
    echo ">>> All images flashed. Pull BOOT0 low, power-cycle and run."
else
    cat <<'EOF'
>>> Flashing failed. The reset is continuous, so software cannot get through.
    Pick one of these hardware workarounds:

  [1] pull BOOT0 high + power off >= 10 s + power on (keep high), then rerun
  [2] disconnect MAX6703A RST from MCU NRST (0R / jumper / trace cut), restore
      afterwards
  [3] feed MAX6703A WDI (PH9) with an external ~10 Hz square wave

  Measuring MCU NRST helps: permanently low -> use [2]; periodic pulses -> [1].
EOF
fi
exit $rc

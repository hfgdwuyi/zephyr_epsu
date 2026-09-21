#!/bin/sh
# flash_when_connected.sh - flash the board while the external MAX6703A watchdog
#                           keeps it in reset
#
# Situation this is for: openocd prints
#     Info : SWD DPIDR 0x6ba02477            (the debug port answers)
#     Info : [stm32h7x.cpu0] external reset detected
#     Error: timed out while waiting for target halted
# The MCU is held in reset by the external MAX6703A because nothing in flash
# toggles its WDI (PH9). The watchdog only starts asserting reset a short while
# after the board is powered, so there is a window right after power-up:
#
#   >>> RUN THIS SCRIPT AND POWER-CYCLE THE BOARD REPEATEDLY <<<
#
# Design rules (learned the hard way):
#   * every openocd run has a TIMEOUT - a hung openocd keeps the ST-Link claimed
#     exclusively and then *every* later openocd blocks for ever;
#   * any openocd this script started is killed on exit;
#   * no mass erase (2 MB takes longer than the watchdog window); each attempt is
#     one short `program`, which only erases the sectors it writes, so an
#     interrupted attempt cannot blank another region;
#   * the application is written first - once it runs it feeds WDI itself.
#
# Usage: ./tools/flash_when_connected.sh [attempts]   (default 60, 0 = forever)
set -u
cd "$(dirname "$0")/.."

BOOT=build-mcuboot/zephyr/zephyr.bin
APP=build/zephyr/zephyr.signed.bin
ATTEMPTS=${1:-60}
STEP_TIMEOUT=15          # seconds per openocd run
LOG=/tmp/flash_when_connected.log

[ -f "$BOOT" ] || { echo "error: $BOOT not found (run ./tools/build_fw.sh --all)"; exit 1; }
[ -f "$APP" ]  || { echo "error: $APP not found (run ./tools/build_fw.sh)";        exit 1; }

OCD_PID=

cleanup() {
	[ -n "$OCD_PID" ] && kill -9 "$OCD_PID" 2>/dev/null
	OCD_PID=
}
trap cleanup EXIT INT TERM

# run with a timeout; output goes to $LOG
run_timeout() {   # $1 = seconds, rest = command
	secs=$1; shift
	: > "$LOG"
	"$@" > "$LOG" 2>&1 &
	OCD_PID=$!
	i=0
	while [ "$i" -lt "$secs" ]; do
		kill -0 "$OCD_PID" 2>/dev/null || break
		sleep 1
		i=$((i + 1))
	done
	if kill -0 "$OCD_PID" 2>/dev/null; then
		kill -9 "$OCD_PID" 2>/dev/null
		wait "$OCD_PID" 2>/dev/null
		OCD_PID=
		echo "(timed out after ${secs}s)" >> "$LOG"
		return 124
	fi
	wait "$OCD_PID" 2>/dev/null
	rc=$?
	OCD_PID=
	return $rc
}

prog() {   # $1=bin  $2=addr ; true when verified
	run_timeout "$STEP_TIMEOUT" openocd -f board/st_nucleo_h745zi.cfg \
		-c "reset_config srst_only srst_nogate" -c "adapter speed 950" \
		-c "init" -c "targets stm32h7x.cpu0" \
		-c "program $1 $2 verify" \
		-c "reset run" \
		-c "shutdown"
	grep -q "Verified OK" "$LOG"
}

app_ok=0
boot_ok=0
i=1
while [ "$ATTEMPTS" -eq 0 ] || [ "$i" -le "$ATTEMPTS" ]; do
	if [ "$ATTEMPTS" -eq 0 ]; then printf '\rattempt %d   ' "$i"
	else printf '\rattempt %d/%d   ' "$i" "$ATTEMPTS"; fi

	# 1) application first: it feeds the watchdog once it runs
	if [ "$app_ok" = "0" ] && prog "$APP" 0x08020000; then
		app_ok=1
		echo
		echo ">>> app written and verified (attempt $i)"
	fi

	# 2) then the bootloader
	if [ "$app_ok" = "1" ] && [ "$boot_ok" = "0" ] && prog "$BOOT" 0x08000000; then
		boot_ok=1
		echo ">>> boot written and verified"
	fi

	if [ "$app_ok" = "1" ] && [ "$boot_ok" = "1" ]; then
		echo
		echo ">>> boot + app written and verified"
		echo ">>> power-cycle the board (BOOT0 low); it should now run and,"
		echo ">>> because the app feeds PH9, stay out of reset."
		exit 0
	fi

	i=$((i + 1))
done

echo
echo "!!! gave up after $ATTEMPTS attempts (app=$app_ok boot=$boot_ok)."
echo "    Last openocd output:"
grep -E "DPIDR|external reset|Error|verified" "$LOG" | tail -4
echo "    If it never connects even while power-cycling, the watchdog holds NRST"
echo "    the whole time - feed PH9 with an external ~10 Hz square wave, or"
echo "    disconnect the MAX6703A reset from MCU NRST, then run this again."
exit 1

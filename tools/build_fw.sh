#!/usr/bin/env bash
#
# build_fw.sh - reproduce the CiosZhong ePSU firmware combo on any machine
#
#   MCUboot bootloader  (build-mcuboot/zephyr/zephyr.bin)
#   App + signed image  (build/zephyr/zephyr.bin, build/zephyr/zephyr.signed.bin)
#
# The serial DFU path only accepts an image signed with the SAME key the
# bootloader was built with, and carrying the SAME layout constants, so all of
# that is centralised here instead of being typed by hand.
#
# Usage:
#   ./tools/build_fw.sh                 # app + sign (normal development loop)
#   ./tools/build_fw.sh --all           # bootloader + app + sign
#   ./tools/build_fw.sh --boot          # bootloader only
#   ./tools/build_fw.sh --pristine      # wipe build/ first (full rebuild)
#   ./tools/build_fw.sh --version 0.2.5 # override the image version
#
# Environment overrides (usually auto-detected):
#   ZEPHYR_BASE, ZEPHYR_SDK_INSTALL_DIR, ZEPHYR_TOOLCHAIN_VARIANT
#   MCUBOOT_DIR   path to the mcuboot checkout (default: $ZEPHYR_BASE/../bootloader/mcuboot)
#   MCUBOOT_KEY   PEM used for signing (default: $MCUBOOT_DIR/root-rsa-2048.pem)
#   PYTHON        python interpreter (default: .venv/bin/python3 then python3)
#
set -euo pipefail

PROJ="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJ"

# ---------------------------------------------------------------- arguments
DO_BOOT=0
DO_APP=1
PRISTINE=0
VERSION_OVERRIDE=""

while [ $# -gt 0 ]; do
	case "$1" in
	--all)      DO_BOOT=1; DO_APP=1 ;;
	--boot)     DO_BOOT=1; DO_APP=0 ;;
	--app)      DO_BOOT=0; DO_APP=1 ;;
	--pristine) PRISTINE=1 ;;
	--version)  shift; VERSION_OVERRIDE="${1:-}" ;;
	-h|--help)  sed -n '2,25p' "$0"; exit 0 ;;
	*)          echo "unknown option: $1 (try --help)" >&2; exit 2 ;;
	esac
	shift
done

# ------------------------------------------------------------ sanity checks
# Zephyr base: env first, then the usual layouts. The env var may be set to a
# stale path, so every candidate is validated before use.
detect_dir() {
	for d in "$@"; do
		[ -n "$d" ] && [ -d "$d" ] && { echo "$d"; return 0; }
		done
	return 1
}

ZEPHYR_BASE=$(detect_dir "${ZEPHYR_BASE:-}" \
	"$HOME/project/02_zephyr/zephyrproject/zephyr" \
	"$HOME/zephyrproject/zephyr" \
	"$HOME/zephyr/zephyr") || \
	fail "cannot find ZEPHYR_BASE (export it explicitly)"
export ZEPHYR_BASE

ZEPHYR_SDK_INSTALL_DIR=$(detect_dir "${ZEPHYR_SDK_INSTALL_DIR:-}" \
	"$HOME/project/02_zephyr"/zephyr-sdk-* \
	"$HOME"/zephyr-sdk-* \
	/opt/zephyr-sdk-*) || \
	fail "cannot find the Zephyr SDK (export ZEPHYR_SDK_INSTALL_DIR)"
export ZEPHYR_SDK_INSTALL_DIR
export ZEPHYR_TOOLCHAIN_VARIANT="${ZEPHYR_TOOLCHAIN_VARIANT:-zephyr}"

MCUBOOT_DIR="${MCUBOOT_DIR:-$(dirname "$ZEPHYR_BASE")/bootloader/mcuboot}"
MCUBOOT_KEY="${MCUBOOT_KEY:-$MCUBOOT_DIR/root-rsa-2048.pem}"
PATCH="$PROJ/mcuboot_patches/0001-cioszhong-mcuboot-local-fixes.patch"
MCUBOOT_CONF="$PROJ/mcuboot_swap_offset.conf"
MCUBOOT_OVERLAY="$PROJ/application/mcuboot_wdi.overlay"
BOARD="cioszhong_psu/stm32h745xx/m7"

if [ -x "$PROJ/.venv/bin/west" ]; then WEST="$PROJ/.venv/bin/west"; else WEST="west"; fi
if [ -x "$PROJ/.venv/bin/python3" ]; then PYTHON="$PROJ/.venv/bin/python3"; else PYTHON="python3"; fi

fail() { echo "ERROR: $*" >&2; exit 1; }

[ -d "$ZEPHYR_BASE" ] || fail "ZEPHYR_BASE not found: $ZEPHYR_BASE"
[ -d "$ZEPHYR_SDK_INSTALL_DIR" ] || fail "Zephyr SDK not found: $ZEPHYR_SDK_INSTALL_DIR"
[ -d "$MCUBOOT_DIR" ] || fail "mcuboot not found: $MCUBOOT_DIR (set MCUBOOT_DIR)"
[ -f "$MCUBOOT_KEY" ] || fail "signing key not found: $MCUBOOT_KEY"
command -v "$WEST" >/dev/null || fail "west not found"

# ------------------------------------------------------- local mcuboot patch
# The patch fixes two things the upgrade path depends on:
#   1. boot/zephyr/main.c    - invalidate caches before reading the image header
#   2. boot/zephyr/watchdog.c- feed the external MAX6703A (WDI PH9)
# plus debug-log cleanups. Apply it once per mcuboot checkout.
apply_patch() {
	[ -f "$PATCH" ] || fail "patch not found: $PATCH"
	if git -C "$MCUBOOT_DIR" apply --reverse --check "$PATCH" >/dev/null 2>&1; then
		echo "== mcuboot patch: already applied"
		return
	fi
	if git -C "$MCUBOOT_DIR" apply --check "$PATCH" >/dev/null 2>&1; then
		git -C "$MCUBOOT_DIR" apply "$PATCH"
		echo "== mcuboot patch: applied"
		return
	fi
	fail "mcuboot patch does not apply cleanly to $MCUBOOT_DIR (checkout modified or wrong revision)"
}

# ---------------------------------------------------------------- version
# Single source of truth: application/Kconfig.project -> CIOS_ZHONG_FW_VERSION
kconfig_version() {
	sed -n '/^config CIOS_ZHONG_FW_VERSION/,/^config /p' "$PROJ/application/Kconfig.project" |
		sed -n 's/^[[:space:]]*default[[:space:]]*"\([^"]*\)".*/\1/p' | head -1
}

APP_VERSION="${VERSION_OVERRIDE:-$(kconfig_version)}"
[ -n "$APP_VERSION" ] || fail "cannot read CIOS_ZHONG_FW_VERSION from application/Kconfig.project"

echo "== project      : $PROJ"
echo "== ZEPHYR_BASE  : $ZEPHYR_BASE"
echo "== mcuboot      : $MCUBOOT_DIR"
echo "== signing key  : $MCUBOOT_KEY"
echo "== image version: $APP_VERSION"

# ------------------------------------------------------------ bootloader
if [ "$DO_BOOT" = 1 ]; then
	echo
	echo "== building MCUboot =="
	apply_patch
	ARGS=(build -d build-mcuboot -b "$BOARD" "$MCUBOOT_DIR/boot/zephyr"
	      -- "-DUSER_CACHE_DIR=$PROJ/build-mcuboot/.zcache"
	         "-DEXTRA_CONF_FILE=$MCUBOOT_CONF"
	         "-DDTC_OVERLAY_FILE=$MCUBOOT_OVERLAY")
	[ "$PRISTINE" = 1 ] && ARGS=(build -p always "${ARGS[@]:1}")
	"$WEST" "${ARGS[@]}"
	echo "== bootloader: build-mcuboot/zephyr/zephyr.bin"
fi

# ------------------------------------------------------------------- app
if [ "$DO_APP" = 1 ]; then
	echo
	echo "== building app =="
	# The app build does NOT sign the image, so the .signed.bin below is
	# regenerated explicitly with the version from Kconfig.
	ARGS=(build -d build -b "$BOARD" application
	      -- "-DUSER_CACHE_DIR=$PROJ/build/.zephyr-cache")
	[ "$PRISTINE" = 1 ] && ARGS=(build -p always "${ARGS[@]:1}")
	"$WEST" "${ARGS[@]}"

	echo
	echo "== signing =="
	rm -f build/zephyr/zephyr.signed.bin
	"$PYTHON" "$MCUBOOT_DIR/scripts/imgtool.py" sign \
		--key "$MCUBOOT_KEY" \
		--header-size 0x400 \
		--align 8 \
		--version "$APP_VERSION" \
		--slot-size 0x80000 \
		build/zephyr/zephyr.bin \
		build/zephyr/zephyr.signed.bin

	# Self check: the DFU path rejects an image without a valid MCUboot
	# header, so fail here rather than on the board.
	"$PYTHON" "$PROJ/tools/check_signed_image.py" \
		build/zephyr/zephyr.signed.bin "$APP_VERSION"
fi

echo
echo "== artifacts =="
[ -f build-mcuboot/zephyr/zephyr.bin ]  && echo "  boot : build-mcuboot/zephyr/zephyr.bin   -> flash @ 0x08000000 (ST-Link)"
[ -f build/zephyr/zephyr.signed.bin ]   && echo "  app  : build/zephyr/zephyr.signed.bin    -> flash @ 0x08020000 (ST-Link)"
[ -f build/zephyr/zephyr.signed.bin ]   && echo "                                          -> tools/psu_dfu.py (serial DFU)"

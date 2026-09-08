#!/bin/sh
# flash_recover_mcuboot.sh — MCUboot + App 烧录脚本（首次/恢复，ST-Link）
#
# E1 样品调试固件组合：
#   MCUboot bootloader  v1.0.0     (SWAP_USING_OFFSET, 2026-09-08 验证)
#   App 固件            v0.2.0     (串口 DFU + MCUboot confirm)
#
# 烧写布局（内部 flash0 = 0x08000000 起 2MB 单设备）：
#   boot    MCUboot        build-mcuboot/zephyr/zephyr.bin   @ 0x08000000
#   app     App (slot0)    build/zephyr/zephyr.signed.bin    @ 0x08020000
# 之后客户升级走串口 DFU（psu_dfu.py → slot1 0x08100000，MCUboot swap）。
#
# 用法：
#   ./flash_recover_mcuboot.sh            # 烧 boot + app
#   ./flash_recover_mcuboot.sh boot       # 只烧 boot
#   ./flash_recover_mcuboot.sh app        # 只烧 app
#
# 说明：
#   - 通过 NRST 硬件复位窗口烧录（reset_config srst_only），自动重试 6 次。
#   - 运行态固件/MAX6703A 看门狗会干扰 SWD：若持续失败，
#     请 BOOT0 拉高 → 断电 ≥10s → 上电（保持高）后再跑本脚本。
set -u
cd "$(dirname "$0")"

BOOT=build-mcuboot/zephyr/zephyr.bin
APP=build/zephyr/zephyr.signed.bin

WANT=${1:-all}   # all | boot | app

flash_once() {   # $1=bin  $2=addr  $3=name
    echo "== 烧录 $3 ($1) @ $2 =="
    openocd -f board/st_nucleo_h745zi.cfg \
        -c "reset_config srst_only" \
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
            echo ">>> $3 烧录成功 (Verified OK)"
            return 0
        fi
        i=$((i + 1))
        sleep 2
    done
    echo "!!! $3 烧录失败：6 次未成功"
    echo "    建议: BOOT0 拉高 → 断电 ≥10s → 上电(保持高) → 重跑本脚本"
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
[ "$rc" -eq 0 ] && echo ">>> 全部烧录成功。BOOT0 拉低后断电重上电即可运行。"
exit $rc

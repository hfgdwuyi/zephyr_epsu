#!/bin/sh
# flash_stlink.sh — 产品板 (STM32H745) 烧录
#
# NRST 已接线：openocd 硬件复位烧录，首选。
# 若首次 openocd 直烧失败（ST-Link/看门狗时序问题），自动重试 openocd
# 烧录同一 .hex（hex 自带地址，向量在 0x08000000 → 直烧固件正确启动）。
#
# 重要：st-flash 只接受裸 .bin，且 bin 不携带地址/可能是 MCUboot slot0
# 布局（前 0x400 为空 header）——用它烧 0x08000000 会得到“烧录成功但
# 不启动”。本脚本不再使用 st-flash，统一走 openocd + .hex。
#
# 用法：
#   ./flash_stlink.sh                     # 烧录 build/zephyr/zephyr.hex
#   ./flash_stlink.sh <path-to.hex>       # 烧录指定 hex
#
# 依赖：openocd（含 board/st_nucleo_h745zi.cfg）

set -e
cd "$(dirname "$0")"

HEX=${1:-build/zephyr/zephyr.hex}
[ -f "$HEX" ] || { echo "error: $HEX not found (run west build first)"; exit 1; }

echo "Flashing $HEX ..."

for i in 1 2 3 4 5 6; do
    echo "=== attempt $i ==="
    if openocd -f board/st_nucleo_h745zi.cfg \
        -c "reset_config srst_only" \
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
echo "提示: 若反复失败，请拔掉 ST-Link USB ≥10s 换口重插；必要时 BOOT0 拉高+断电重上电再试。"
exit 1

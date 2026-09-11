#!/bin/sh
# flash_diag.sh — STM32H745 SWD 连接诊断（换板/连不上时先跑这个）
#
# 用法：
#   ./tools/flash_diag.sh            # 默认跑全部诊断
#   ./tools/flash_diag.sh quick      # 只跑主策略（最快）
#
# 输出：每项策略 PASS/FAIL + 最后结论。供换板排查：
#   全部 FAIL + 电压正常  → 芯片侧问题（RDP 锁 / 固件占 SWD / 虚焊）
#   BOOT0 高能连上         → 板上固件把 PA13/14 复用成 GPIO 了
#   完全无电压             → SWD 供电/接线问题
set -u
cd "$(dirname "$0")/.."

LOG=/tmp/flash_diag.log
: > "$LOG"

pass=0
fail=0

try() {   # $1=策略名  其余=openocd 参数
    name=$1; shift
    if openocd "$@" 2>&1 | tee -a "$LOG" | grep -qE "target voltage|Target voltage"; then
        echo "PASS"
    else
        echo "FAIL"
    fi
}

echo "====  ST-Link probe 识别 ===="
if openocd -f interface/stlink-dap.cfg -c "transport select dapdirect_swd" \
        -c "adapter speed 100" -c "init" -c "shutdown" \
        2>&1 | tee -a "$LOG" | grep -q "STLINK V2\|STLINK-V3\|STLINK V3"; then
    echo "[1] probe 识别: PASS"
    pass=$((pass+1))
else
    echo "[1] probe 识别: FAIL (检查 USB / 驱动)"
    fail=$((fail+1))
fi

echo
echo "====  SWD 连接策略 ===="
v=$(grep -oE "Target voltage: [0-9.]+" "$LOG" | head -1)
echo "    目标电压: ${v:-N/A}"

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
echo "====  结果: 连接 PASS=$((pass-1)) FAIL=$fail (probe 识别不计入) ===="
echo
if [ "$pass" -ge 2 ]; then
    echo ">>> 至少一种策略能连上。烧录请用 flash_recover_mcuboot.sh / flash_stlink.sh"
    echo "    若仅 BOOT0 高时能连：板上固件占用了 PA13/14 (SWDIO/SWCLK)，"
    echo "    需先擦除/烧录引导固件，或检查固件 GPIO 配置。"
elif grep -q "Target voltage:" "$LOG"; then
    echo ">>> 全部 FAIL 但目标电压正常 → 芯片侧问题："
    echo "    1) RDP 读保护级别1/2（级别2永久锁死，需换芯片）"
    echo "    2) 芯片虚焊/损坏，或型号不是 H745"
    echo "    3) SWDIO/SWCLK 到芯片引脚断线（NRST 高不代表 SWD 通）"
    echo "    建议：BOOT0 拉高再跑一次本脚本；仍 FAIL 则查 RDP/换芯片。"
else
    echo ">>> 全部 FAIL 且无目标电压 → 接线/供电问题："
    echo "    检查 SWDIO/SWCLK/GND/3V3、ST-Link USB、目标供电。"
fi
echo
echo "完整日志: $LOG"

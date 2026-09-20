#!/bin/sh
# flash_window.sh — 方案 C：抢复位窗口 + 喂狗小固件，用来救"擦成空片后烧不进去"的板子
#
# 症状（这块板子遇到的现象）：
#   app 被 mass_erase 擦掉后，没有任何固件翻转 PH9(WDI)，
#   外部看门狗 MAX6703A（1.6 s）就周期性拉 NRST，于是 openocd 一直是：
#       Info : [stm32h7x.cpu0] external reset detected
#       Error: timed out while waiting for target halted
#   内核 halt 不住 → 任何 flash 操作都做不了 → 死循环。
#   （实测依据：WWDG1_CR=0 说明不是芯片自己复位；AHB 读 0x08000000 直接
#     "Failed to read memory"，只有调试域可用 —— 即芯片被摁在复位里。）
#
# 本脚本怎么救：
#   ① 反复抢复位间隙（每次一个独立 openocd 会话）：
#        init → halt → 把 tools/wdi_feeder（164 字节固化程序）载入 RAM
#        → 设 SP/PC → resume
#      该固件把 PH9 配成 TIM12_CH2 的 ~50 Hz PWM。定时器是硬件外设，
#      **之后即使 openocd 把内核 halt 住去烧 flash，PH9 仍被硬件持续翻转**，
#      openocd 的 H7 配置只冻结 WWDG/WDGLSD、不冻结 TIM12，所以看门狗被稳住。
#   ② 自检："halt 3 秒不被复位" → 说明喂狗已生效；随后正常烧 boot + app
#      （不 mass_erase：每个 program 只擦自己要用的扇区，失败也不会误擦别的区域）。
#   ③ 万一 PWM 没生效（自检失败），退回「分块写 + 写完立刻 resume」的慢速路径，
#      同样不擦除（前提：目标扇区已是空片）。
#
# 适用条件：复位是**脉冲式**、有空隙。若 NRST 被持续拉低，脚本会抢不到窗口并
#           打印硬件方案（断 RESET 线 / 外部喂 WDI）。
#           BOOT0 与此无关：它只决定启动来源，不喂狗、不影响 NRST。
#
# 与 tools/flash_when_connected.sh 的关系：那个是"纯抢窗口"的简化版（不带喂狗
#   固件，每个窗口只够烧一部分）；本脚本先用喂狗固件把复位彻底稳住，因此能
#   一次烧完并自检。
#
# 用法：
#   ./tools/flash_window.sh            # 抢窗口最多 90 秒
#   ./tools/flash_window.sh 180        # 抢窗口最多 180 秒
#   （板子若还有电：运行脚本后断电 ≥10 s 再上电，窗口更干净）
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)

BOOT=build-mcuboot/zephyr/zephyr.bin
APP=build/zephyr/zephyr.signed.bin
FEEDER_DIR=tools/wdi_feeder
FEEDER=$FEEDER_DIR/wdi_feeder.bin
FEEDER_RAM=0x24000000          # AXI SRAM(D1) 起始
FEEDER_SP=0x24008000
FEEDER_PC=0x24000001
CFG=board/st_nucleo_h745zi.cfg
SPEED=950
STEP_TIMEOUT=20                # 每次 openocd 的硬超时（秒）
WINDOW_SEC=${1:-90}
LOG=/tmp/flash_window.log

die() { echo "!!! $*" >&2; exit 1; }

# ---------- openocd 运行器：带硬超时 + 退出清理 ----------
# 卡死的 openocd 会独占 ST-Link，导致之后每个 openocd 都永远阻塞，
# 所以每次调用都必须有超时，且脚本退出时必须确保它被杀掉。
OCD_PID=
cleanup() { [ -n "$OCD_PID" ] && kill -9 "$OCD_PID" 2>/dev/null; OCD_PID=; }
trap cleanup EXIT INT TERM

oc() {                          # $@ = openocd 的 -c 参数；输出进 $LOG
	: > "$LOG"
	openocd -f "$CFG" -c "adapter speed $SPEED" "$@" -c "shutdown" >"$LOG" 2>&1 &
	OCD_PID=$!
	i=0
	while [ "$i" -lt "$STEP_TIMEOUT" ]; do
		kill -0 "$OCD_PID" 2>/dev/null || break
		sleep 1
		i=$((i + 1))
	done
	if kill -0 "$OCD_PID" 2>/dev/null; then
		kill -9 "$OCD_PID" 2>/dev/null; wait "$OCD_PID" 2>/dev/null
		OCD_PID=; echo "(timed out after ${STEP_TIMEOUT}s)" >>"$LOG"; return 124
	fi
	wait "$OCD_PID" 2>/dev/null; rc=$?; OCD_PID=
	return $rc
}

oc_ok() {                       # 上一次 oc 是否"没有致命错误"
	! grep -qE "^Error|timed out while waiting" "$LOG"
}

# ---------- 前置检查 ----------
[ -f "$BOOT" ] || die "$BOOT 不存在（先构建 bootloader）"
[ -f "$APP" ]  || die "$APP 不存在（先构建并签名 app）"
command -v openocd >/dev/null 2>&1 || die "找不到 openocd"
pkill -9 openocd 2>/dev/null
sleep 1

# ---------- 工具链 / Zephyr 源码定位（环境变量可能是失效路径，逐个验证） ----------
find_sdk() {
	for c in "${ZEPHYR_SDK_INSTALL_DIR:-}" "$HOME/project/02_zephyr/zephyr-sdk-1.0.1" \
		 "$HOME"/project/02_zephyr/zephyr-sdk-*; do
		[ -n "$c" ] && [ -x "$c/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" ] && { echo "$c"; return 0; }
	done
	return 1
}
find_zephyr() {
	for c in "${ZEPHYR_BASE:-}" "$HOME/project/02_zephyr/zephyrproject/zephyr" \
		 "$HOME"/project/*/zephyrproject/zephyr; do
		[ -n "$c" ] && [ -f "$c/include/zephyr/kernel.h" ] && { echo "$c"; return 0; }
	done
	return 1
}

# ---------- 喂狗固件：缺失或 .c 更新过就重编 ----------
if [ ! -f "$FEEDER" ] || [ "$FEEDER_DIR/wdi_feeder.c" -nt "$FEEDER" ]; then
	SDK=$(find_sdk) || die "找不到 arm-zephyr-eabi 工具链（可设 ZEPHYR_SDK_INSTALL_DIR）"
	ZP=$(find_zephyr) || die "找不到 Zephyr 源码树（可设 ZEPHYR_BASE）"
	MOD=$(dirname "$ZP")/modules
	echo "== 编译喂狗固件（SDK=${SDK}）=="
	"$SDK/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" \
		-mcpu=cortex-m7 -mthumb -O2 -ffreestanding -nostdlib -fno-builtin \
		-I"$MOD/hal/stm32/stm32cube/stm32h7xx/soc" \
		-I"$MOD/hal/cmsis/CMSIS/Core/Include" \
		-T "$FEEDER_DIR/wdi_feeder.ld" -Wl,--gc-sections \
		-o "$FEEDER_DIR/wdi_feeder.elf" "$FEEDER_DIR/wdi_feeder.c" || die "喂狗固件编译失败"
	"$SDK/arm-zephyr-eabi/bin/arm-zephyr-eabi-objcopy" -O binary \
		"$FEEDER_DIR/wdi_feeder.elf" "$FEEDER" || die "objcopy 失败"
fi
echo "喂狗固件: $FEEDER ($(wc -c <"$FEEDER" | tr -d ' ') 字节)"

# ---------- 阶段 1：抢窗口，把喂狗固件跑起来 ----------
echo
echo "===== 阶段 1：抢复位窗口并运行喂狗固件（最多 ${WINDOW_SEC}s）====="
echo "（板子还有电的话：现在断电 ≥10s 再上电，能显著提高成功率）"

deadline=$(( $(date +%s) + WINDOW_SEC ))
try=0; feeder_ok=0
while [ "$(date +%s)" -lt "$deadline" ]; do
	try=$((try + 1))
	if oc -c "init" -c "halt" \
	      -c "load_image $FEEDER $FEEDER_RAM bin" \
	      -c "verify_image $FEEDER $FEEDER_RAM bin" \
	      -c "reg sp $FEEDER_SP" -c "reg pc $FEEDER_PC" -c "resume" && oc_ok; then
		printf "\r  第 %d 次尝试：抢到 halt，喂狗固件已载入并运行        \n" "$try"
		feeder_ok=1
		break
	fi
	printf "\r  第 %d 次尝试：未抢到（%s）   " "$try" \
		"$(grep -m1 -oE 'timed out while waiting for target halted|Error: .*' "$LOG" | cut -c1-56)"
	sleep 0.3
done

if [ "$feeder_ok" != 1 ]; then
	echo
	echo "!!! ${WINDOW_SEC}s 内始终 halt 不住内核 —— 复位没有可用空隙（NRST 可能被持续拉低）"
	echo "    软件方案到此为止，请走硬件之一："
	echo "      a) 给 WDI(PH9 网络) 喂外部 ~10Hz 3.3V 方波（最省事，不动烙铁）"
	echo "      b) 断开 MAX6703A 的 RESET 到 MCU NRST 的连线（0R/跳线/割线），烧完恢复"
	echo "      c) 先用表量 NRST：常低 → 用 b)；每 ~1.6s 一个 ~200ms 脉冲 → 用 a)"
	exit 2
fi

# ---------- 自检：halt 3 秒是否还会被复位 ----------
echo "  自检：把内核 halt 住 3 秒，看是否还会被外部看门狗复位 ..."
sleep 0.5
if oc -c "init" -c "halt" -c "sleep 3000" -c "reg pc" && oc_ok; then
	echo "  OK：3 秒内未被复位 → PH9 正被硬件 PWM(50Hz) 翻转，烧写可以一气做完。"
	AUTONOMY=1
else
	echo "  !! 自检失败：PWM 没能喂住看门狗（PH9 的 AF2/TIM12 配置没生效？）"
	echo "     改用「分块写 + 写完立刻 resume」的慢速路径（不擦除）。"
	AUTONOMY=0
fi

# ---------- 阶段 2：烧写 ----------
echo
if [ "$AUTONOMY" = 1 ]; then
	echo "===== 阶段 2：烧写 boot + app（不 mass_erase，program 只擦用到的扇区）====="
	ok=0; i=1
	while [ "$i" -le 4 ]; do
		echo "== 第 $i 次尝试 =="
		if oc -c "init" -c "halt" \
		      -c "program $BOOT 0x08000000 verify" \
		      -c "program $APP 0x08020000 verify" \
		      -c "reset run" && grep -q "Verified OK" "$LOG"; then
			grep -E "Verified OK|Erased|wrote" "$LOG" | tail -4
			ok=1; break
		fi
		grep -E "^Error|Verified OK|timed out" "$LOG" | tail -4
		i=$((i + 1)); sleep 1
	done
	[ "$ok" = 1 ] || die "烧写失败（看门狗又打断了会话）"
	echo ">>> 完成：MCUboot @0x08000000 + app @0x08020000 均已 verify"
else
	echo "===== 阶段 2（慢速）：分块写，不擦除 ====="
	echo "前提：目标扇区已是空片（之前的 mass_erase 成功过）"
	tmp=$(mktemp -d) || die "mktemp 失败"
	trap 'cleanup; rm -rf "$tmp"' EXIT INT TERM
	chunk=4096
	write_chunked() {   # $1=文件 $2=基址
		f=$1; base=$2; off=0
		size=$(wc -c <"$f" | tr -d ' ')
		while [ "$off" -lt "$size" ]; do
			part=$tmp/$(basename "$f").$off
			dd if="$f" of="$part" bs=$chunk skip=$((off / chunk)) count=1 2>/dev/null
			addr=$(printf '0x%X' $((base + off)))
			if ! oc -c "init" -c "halt" -c "flash write_image $part $addr bin" \
				-c "reg pc $FEEDER_PC" -c "resume" || ! oc_ok; then
				tail -3 "$LOG"; die "写 $addr 失败"
			fi
			printf "\r  %-28s @ %s   " "$(basename "$f")" "$addr"
			off=$((off + chunk))
		done
		echo
	}
	write_chunked "$BOOT" 0x08000000
	write_chunked "$APP"  0x08020000
	echo ">>> 分块写入完成（未 verify；稳妥起见可事后用 SWD 再校验一次）"
fi

# ---------- 收尾 ----------
cat <<'EOF'

===== 完成 =====
  1) BOOT0 若被拉高过：恢复为低，然后断电 ≥10 s 再上电。
  2) 串口应看到启动横幅（含 "PSU CMD: ready"）；之后 app 自己每 500ms 翻 PH9，
     外部看门狗不再复位，SWD 与串口都恢复常态。
  3) 后续升级请走串口 DFU（无需 SWD）：
       .venv/bin/python tools/psu_dfu.py /dev/cu.usbserial-130 build/zephyr/zephyr.signed.bin
EOF

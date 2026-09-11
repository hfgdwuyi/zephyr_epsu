#!/bin/sh
# flash_force.sh — 复位循环/看门狗干扰下的强制烧录（抢窗口多策略）
#
# 适用场景：板上 MAX6703A 外部看门狗在持续复位 MCU（app 起不来、没人喂狗），
#          常规单次 openocd 命令连 halt 都保持不住（"timed out while waiting
#          for target halted"）。本脚本轮换多种连接/复位策略并高频重试，
#          尽可能抓住复位间隙完成烧录。
#
# 用法：
#   ./flash_force.sh              # 烧 boot + app
#   ./flash_force.sh app          # 只烧 app
#   ./flash_force.sh boot         # 只烧 boot
#   ./flash_force.sh app 40       # 指定每个策略的重试次数（默认 20）
#
# 若全部失败，说明复位是**持续**的（不是间隙），必须硬件处理：
#   1) BOOT0 拉高 + 断电 ≥10s + 上电
#   2) 断开 MAX6703A 的 RST → MCU NRST 连接
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

# 先清理可能残留的 openocd（会占用 ST-Link 与端口，导致莫名失败）
pkill -9 openocd 2>/dev/null
sleep 1

flash_one() {   # $1=file $2=addr $3=策略号 $4=速度 $5..=额外 openocd 参数
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
        # 轮换 4 种策略：标准 / 高速 / 不碰NRST / 软件复位
        case $((s % 4)) in
            1) set -- 950  ;;
            2) set -- 4000 ;;
            3) set -- 950 -c "reset_config none" ;;
            0) set -- 950 -c "cortex_m reset_config sysresetreq" ;;
        esac

        printf "\r  尝试 %2d/%d (策略%s) ... " "$s" "$TRIES" "$(( (s % 4) + 1 ))"
        out=$(flash_one "$f" "$a" "$s" "$@" 2>&1)
        if echo "$out" | grep -q "Verified OK"; then
            printf "\n>>> %s 烧录成功 (第 %d 次尝试)\n" "$base" "$s"
            return 0
        fi
        sleep 0.3
    done
    printf "\n!!! %s 失败：%d 次尝试均未成功\n" "$base" "$TRIES"
    return 1
}

rc=0
for t in $TARGETS; do
    file=${t%@*}
    addr=${t#*@}
    echo "== 烧录 $file @ $addr =="
    try_target "$file" "$addr" || rc=1
done

echo
if [ "$rc" -eq 0 ]; then
    echo ">>> 全部烧录成功。BOOT0 拉低后断电重上电即可运行。"
else
    cat <<'EOF'
>>> 烧录失败。复位是持续的，纯软件无法穿透，请任选一种硬件处理：

  [1] BOOT0 拉高 + 断电 ≥10 秒 + 上电（保持高）→ 再跑本脚本
  [2] 断开板上 MAX6703A 的 RST → MCU NRST 连接（0Ω/跳线/割线）→ 烧完恢复
  [3] 给 MAX6703A 的 WDI(PH9) 外接 10Hz 左右方波持续喂狗

  另可用万用表量 MCU NRST：持续低 → 必须用 [2]；周期脉冲 → [1] 可行。
EOF
fi
exit $rc

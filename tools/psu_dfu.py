#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
psu_dfu.py — CiosZhong PSU 串口固件升级工具（支持连续多次烧录 / 可靠性测试）

通过 USART1 (PB14/PB15) 把签名固件 (zephyr.signed.bin) 发送给运行中的 APP，
APP 写入 slot1 并请求 MCUboot swap，复位后自动运行新固件。

协议（与 uart_cmd.c dfu 一致）：
  dfu            → APP 擦除 slot1 并进入升级状态
  size <hex>     → 声明固件总字节数
  data <off> <hex...> → 带偏移的 512B hex 块；逐块 ACK，ERR/超时重发
  全部发送后 APP 自动 boot_request_upgrade + 复位

用法：
  单次： python3 psu_dfu.py <serial-port> <signed-firmware.bin>
  循环： python3 psu_dfu.py <serial-port> <signed-firmware.bin> --count 50
         （连续升级 50 次做可靠性测试；每次自动重开串口等待重启完成）
  其它： --stop-on-fail   任一次失败立即停止
        --interval SEC    每轮之间额外等待（默认 0）
        --version-check   每轮后读一次 app 版本横幅确认固件已运行（默认开）

依赖：pyserial
"""
import argparse
import sys
import time

import serial

BLOCK = 512  # 每行数据字节数（与固件 uart_cmd.c DFU_BLOCK_MAX 一致）


# --------------------------------------------------------------------------
# 串口读取辅助
# --------------------------------------------------------------------------
def read_until_ack(ser, timeout=5):
    """读串口直到出现 'ACK ' 或 'ERR'（DFU 应答行）。"""
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting)
            buf += data
            if b"ACK " in buf or b"ERR" in buf:
                return buf.decode(errors="replace")
        time.sleep(0.01)
    return buf.decode(errors="replace")


def read_until(ser, token, timeout=10):
    """读串口直到出现 token，返回期间收到的文本。"""
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting)
            buf += data
            if token.encode() in buf:
                return buf.decode(errors="replace")
        time.sleep(0.02)
    return buf.decode(errors="replace")


# --------------------------------------------------------------------------
# 单次升级
# --------------------------------------------------------------------------
def upgrade_once(ser, image, verbose=True):
    """执行一次完整 DFU 上传 + 触发 reboot。

    返回: True 成功（上传 + 收到 rebooting）；False 失败。
    调用方负责在 reboot 后重新打开串口等待新固件就绪。
    """
    total = len(image)
    total_blocks = (total + BLOCK - 1) // BLOCK

    # 1. 进入 dfu（先清残留，保证干净握手）
    ser.write(b"\r\n\r\n")
    time.sleep(0.1)
    ser.reset_input_buffer()
    ser.write(b"dfu\r\n")
    resp = read_until(ser, "size <hex>", 30)
    if "size <hex>" not in resp:
        print(f"  [FAIL] APP 未进入 DFU 模式: {resp.strip()[:80]}")
        return False

    # 2. size
    ser.write(f"size {total:X}\r\n".encode())
    resp = read_until(ser, "data <hex>", 10)
    if "data <hex>" not in resp:
        print(f"  [FAIL] size 应答异常: {resp.strip()[:80]}")
        return False

    # 3. 分块发送
    sent = 0
    for idx in range(total_blocks):
        off = idx * BLOCK
        chunk = image[off:off + BLOCK]
        last = (idx == total_blocks - 1)

        if last:
            # 末块：固件写完直接 rebooting（无 ACK）
            ser.write(f"data {off:X} ".encode() + chunk.hex().encode() + b"\r\n")
            resp = read_until(ser, "rebooting", 40)
            if "rebooting" in resp:
                sent += len(chunk)
                if verbose:
                    print(f"  sent {sent}/{total}")
                    print(f"  [DONE] {resp.strip()[:80]}")
                return True
            print(f"  [FAIL] 末块无 rebooting 响应: {resp.strip()[:80]}")
            return False

        ok = False
        for attempt in range(50):
            ser.write(f"data {off:X} ".encode() + chunk.hex().encode() + b"\r\n")
            resp = read_until_ack(ser, 5)
            if f"ACK {off + len(chunk):X}" in resp:
                sent += len(chunk)
                ok = True
                break
            ser.reset_input_buffer()  # 坏行/超时 → 重发同块
        if not ok:
            print(f"  [FAIL] 块 0x{off:X} 多次失败，放弃")
            return False
        if verbose and (sent % 16384 == 0 or sent >= total):
            print(f"  sent {sent}/{total}")

    return False  # 理论不可达（末块已 return）


# --------------------------------------------------------------------------
# 等待重启后的固件就绪（读到启动横幅/PSU CMD ready）
# --------------------------------------------------------------------------
def wait_app_ready(ser, timeout=15):
    """复位后等 APP 起来：读到 'PSU CMD: ready' 视为成功。"""
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting)
            buf += data
            if b"PSU CMD: ready" in buf:
                return True, buf.decode(errors="replace")
        time.sleep(0.05)
    return False, buf.decode(errors="replace")


# --------------------------------------------------------------------------
# 主流程：单次或连续 N 次
# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="CiosZhong PSU 串口固件升级（可靠性测试）")
    ap.add_argument("port", help="串口，如 /dev/cu.usbserial-110")
    ap.add_argument("firmware", help="签名固件 zephyr.signed.bin")
    ap.add_argument("--count", type=int, default=1, help="连续升级次数（默认 1）")
    ap.add_argument("--stop-on-fail", action="store_true", help="任一次失败立即停止")
    ap.add_argument("--interval", type=float, default=0.0,
                    help="每轮之间额外等待秒数（默认 0）")
    ap.add_argument("--quiet", action="store_true", help="减少打印")
    args = ap.parse_args()

    with open(args.firmware, "rb") as f:
        image = f.read()
    total = len(image)
    print(f"固件: {args.firmware} ({total} bytes)")
    print(f"串口: {args.port}   连续次数: {args.count}")

    ok_count = 0
    fail_count = 0
    t_start = time.time()

    for i in range(1, args.count + 1):
        print(f"\n===== 第 {i}/{args.count} 次升级 =====")
        # 每次独立连接（reboot 后旧连接会失效/需重开）
        try:
            ser = serial.Serial(args.port, 115200, timeout=0.1)
            ser.reset_input_buffer()
        except serial.SerialException as e:
            print(f"  [FAIL] 打开串口失败: {e}")
            fail_count += 1
            if args.stop_on_fail:
                break
            time.sleep(2)
            continue

        try:
            success = upgrade_once(ser, image, verbose=not args.quiet)
            ser.close()  # 上传完先关，等重启

            if success:
                # 重启后等 APP ready（重开串口）
                time.sleep(1.5)  # 给 MCUboot swap + 启动留时间
                ser2 = serial.Serial(args.port, 115200, timeout=0.1)
                ready, _ = wait_app_ready(ser2, timeout=15)
                ser2.close()
                if ready:
                    ok_count += 1
                    print(f"  [OK] 第 {i} 次成功，APP 已就绪")
                else:
                    # 上传成功但 APP 没确认就绪：算失败
                    print(f"  [FAIL] 上传成功但 APP 未就绪（可能 swap/启动异常）")
                    fail_count += 1
                    if args.stop_on_fail:
                        break
            else:
                fail_count += 1
                if args.stop_on_fail:
                    break
        except serial.SerialException as e:
            print(f"  [FAIL] 串口错误: {e}")
            fail_count += 1
            if args.stop_on_fail:
                break

        if args.interval > 0 and i < args.count:
            print(f"  等待 {args.interval}s ...")
            time.sleep(args.interval)

    elapsed = time.time() - t_start
    print("\n================= 结果汇总 =================")
    print(f"总次数: {args.count}   成功: {ok_count}   失败: {fail_count}")
    if ok_count + fail_count > 0:
        print(f"成功率: {ok_count * 100.0 / (ok_count + fail_count):.1f}%")
    print(f"总耗时: {elapsed:.1f}s")
    sys.exit(0 if fail_count == 0 else 1)


if __name__ == "__main__":
    main()

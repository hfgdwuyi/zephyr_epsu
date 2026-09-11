#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
psu_temp.py — 读取 CiosZhong PSU 板载 TMP75 温度（USART1 命令通道）

硬件：TMP75 @ I2C1 (SCL=PB8 / SDA=PB9)，7 位地址 0x48
协议：串口发 `temp\\r\\n`，固件回 `temp=<v>.<vvv> C (raw_ok, errs=N)`

用法：
  python3 psu_temp.py /dev/cu.usbserial-XXXX            # 读一次
  python3 psu_temp.py /dev/cu.usbserial-XXXX --count 0  # 连续监控（Ctrl-C 退出）
  python3 psu_temp.py /dev/cu.usbserial-XXXX --count 20 --interval 0.5

依赖：pyserial
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("error: 需要 pyserial (pip install pyserial)")
    sys.exit(1)

BAUD = 115200


def read_once(ser, timeout=2.0, verbose=False):
    """发一条 temp 命令，返回回复文本（None=超时）。"""
    ser.reset_input_buffer()
    ser.write(b"temp\r\n")

    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        if ser.in_waiting:
            buf += ser.read(ser.in_waiting)
            if b"temp=" in buf or b"ERR" in buf:
                break
        time.sleep(0.02)

    text = buf.decode(errors="replace").strip()
    if verbose and text:
        print(f"  raw: {text!r}")
    return text or None


def main():
    ap = argparse.ArgumentParser(description="读取板载 TMP75 温度")
    ap.add_argument("port", help="串口设备，如 /dev/cu.usbserial-XXXX")
    ap.add_argument("--count", type=int, default=1,
                    help="读取次数；0 = 无限循环（默认 1）")
    ap.add_argument("--interval", type=float, default=1.0,
                    help="连续模式间隔秒数（默认 1.0）")
    ap.add_argument("--verbose", action="store_true", help="打印原始回复")
    args = ap.parse_args()

    try:
        ser = serial.Serial(args.port, BAUD, timeout=1)
    except Exception as exc:
        print(f"error: 打开串口失败: {exc}")
        sys.exit(1)

    print(f"TMP75 温度读取 @ {args.port} ({BAUD} baud)  Ctrl-C 退出")

    n = 0
    try:
        while args.count == 0 or n < args.count:
            n += 1
            resp = read_once(ser, verbose=args.verbose)

            if resp is None:
                print(f"[{n:4d}] 超时无回复（APP 在跑吗？串口接 USART1 PB14/PB15 吗？）")
            elif resp.startswith("temp="):
                # 正常：temp=25.375 C (raw_ok, errs=0)
                value = resp.split()[0].split("=", 1)[1]
                errs = ""
                if "errs=" in resp:
                    errs = " errs=" + resp.split("errs=")[1].rstrip(")")
                print(f"[{n:4d}] {value} °C{errs}")
            elif "ERR" in resp:
                print(f"[{n:4d}] 固件报错: {resp}")
            else:
                print(f"[{n:4d}] 未识别回复: {resp!r}")

            if args.count == 0 or n < args.count:
                time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\n已停止。")
    finally:
        ser.close()


if __name__ == "__main__":
    main()

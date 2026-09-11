#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
psu_monitor.py — CiosZhong PSU ADC 通道实时监控（命令行版）

被动接收固件每秒推送的 SENSOR 行，在终端里实时刷新成一张表。
不依赖图形界面，适合脚本化/远程验证。

用法：
  python3 psu_monitor.py                     # 自动找串口
  python3 psu_monitor.py /dev/cu.usbserial-130
  python3 psu_monitor.py /dev/cu.usbserial-130 --raw   # 同时打印原始行

依赖：pyserial
"""
import argparse
import re
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("error: 需要 pyserial")
    sys.exit(1)

BAUD = 115200

CHANNELS = [
    ("temp1", "PA3", "°C"), ("temp2", "PA4", "°C"),
    ("adc_12v", "PH2", "V"), ("adc_pdc7", "PF11", "V"),
    ("adc_pdc6", "PF12", "V"), ("adc_pdc5", "PF13", "V"),
    ("adc_5v0", "PF14", "V"), ("adc_pdc0", "PA6", "V"),
    ("adc_pdc4", "PA0_C", "V"), ("adc_pdc2", "PB0", "V"),
    ("adc_pdc3", "PB1", "V"), ("adc_3v3", "PC0", "V"),
    ("adc_pdc1", "PC2", "V"), ("adc_pdc0_alt", "PC3_C", "V"),
    ("adc_vin", "PC2_C", "V"),
]

RE_SENSOR = re.compile(r"SENSOR\s+(\w+)\s*:\s*(.+?)\s*$")


def render(values, rounds, status):
    out = ["\033[H\033[J"]   # 清屏
    out.append("CiosZhong PSU — ADC 通道实时监控")
    out.append("=" * 52)
    out.append(f"{'通道':<16}{'引脚':<8}{'数值':>14}{'':<3}{'更新':<10}")
    out.append("-" * 52)
    for name, pin, unit in CHANNELS:
        v, ts = values.get(name, ("—", ""))
        out.append(f"{name:<16}{pin:<8}{v:>14}  {unit:<3}{ts:<10}")
    out.append("-" * 52)
    out.append(status)
    out.append("")
    out.append(f"已完成 {rounds} 轮上报    Ctrl-C 退出")
    print("\n".join(out), flush=True)


def main():
    ap = argparse.ArgumentParser(description="ADC 通道实时监控")
    ap.add_argument("port", nargs="?")
    ap.add_argument("--raw", action="store_true", help="同时打印原始串口行")
    args = ap.parse_args()

    port = args.port
    if not port:
        ports = [p.device for p in list_ports.comports()
                 if "usbserial" in p.device or "USB" in p.device or "SLAB" in p.device]
        port = ports[0] if ports else None
    if not port:
        print("error: 找不到串口，请手动指定")
        sys.exit(1)

    ser = serial.Serial(port, BAUD, timeout=0.3)
    print(f"已连接 {port} @ {BAUD}，等待固件推送...")

    values = {}
    rounds = 0
    n_lines = 0
    last_rx = time.time()
    render(values, rounds, f"等待数据... ({port})")

    try:
        while True:
            data = ser.read(256)
            if data:
                last_rx = time.time()
                for line in data.decode(errors="replace").splitlines():
                    if not line.strip():
                        continue
                    if args.raw:
                        print("   raw:", line)
                    m = RE_SENSOR.match(line)
                    if not m:
                        continue
                    name, val = m.group(1), m.group(2)
                    for cn, _, _ in CHANNELS:
                        if cn == name:
                            values[name] = (val.replace(" °C", "").replace(" V", "")
                                            .replace(" (no AC)", ""),
                                            time.strftime("%H:%M:%S"))
                            n_lines += 1
                            break
            # 每收满一轮就重绘
            if n_lines >= len(CHANNELS):
                rounds += n_lines // len(CHANNELS)
                n_lines = n_lines % len(CHANNELS)
                render(values, rounds,
                       f"已连接 {port} | 最后接收 {time.strftime('%H:%M:%S', time.localtime(last_rx))}")
            elif time.time() - last_rx > 3:
                render(values, rounds, f"⚠ {int(time.time()-last_rx)}s 无数据（板子在跑吗？串口对吗？）")
                last_rx = time.time()
    except KeyboardInterrupt:
        print("\n已停止。")
    finally:
        ser.close()


if __name__ == "__main__":
    main()

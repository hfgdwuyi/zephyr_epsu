#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_k_outputs.py — 串口控制测试：K8_1/K8_2/K9/K10/K11/K12 全高

用法: .venv/bin/python3 tools/test_k_outputs.py [串口]
默认串口: /dev/cu.usbserial-110
"""
import serial
import sys
import time

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbserial-110"

# K 编号 → DOUT 软件索引（reg）
# K10=23 K11=24 K12=25 K8_1=26 K8_2=27 K9=28
MASK = (1 << 23) | (1 << 24) | (1 << 25) | (1 << 26) | (1 << 27) | (1 << 28)
MASK_HEX = f"{MASK:X}"

def cmd(ser, s, wait=0.4):
    ser.write(s.encode() + b"\r\n")
    time.sleep(wait)
    return ser.read(ser.in_waiting).decode(errors="replace").strip()

ser = serial.Serial(PORT, 115200, timeout=1)
time.sleep(0.2)

# 清残留
ser.write(b"\r\n\r\n")
time.sleep(0.2)
ser.reset_input_buffer()

print("=== 置位前 getdout ===")
print(cmd(ser, "getdout"))

print(f"=== doutall {MASK_HEX} (K10,K11,K12,K8_1,K8_2,K9 全高) ===")
print(cmd(ser, f"doutall {MASK_HEX}"))

print("=== 置位后 getdout ===")
print(cmd(ser, "getdout"))

ser.close()
print("完成。若置位后显示 0x1F800000 表示软件控制正常。")

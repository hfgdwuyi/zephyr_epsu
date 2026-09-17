#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_k_outputs.py - drive K8_1/K8_2/K9/K10/K11/K12 high over the serial port

Usage: .venv/bin/python3 tools/test_k_outputs.py [port]
Default port: /dev/cu.usbserial-110
"""
import serial
import sys
import time

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbserial-110"

# K name -> DOUT software index (reg)
# K10=23 K11=24 K12=25 K8_1=26 K8_2=27 K9=28
MASK = (1 << 23) | (1 << 24) | (1 << 25) | (1 << 26) | (1 << 27) | (1 << 28)
MASK_HEX = f"{MASK:X}"

def cmd(ser, s, wait=0.4):
    ser.write(s.encode() + b"\r\n")
    time.sleep(wait)
    return ser.read(ser.in_waiting).decode(errors="replace").strip()

ser = serial.Serial(PORT, 115200, timeout=1)
time.sleep(0.2)

# clear leftovers
ser.write(b"\r\n\r\n")
time.sleep(0.2)
ser.reset_input_buffer()

print("=== getdout before ===")
print(cmd(ser, "getdout"))

print(f"=== doutall {MASK_HEX} (K10,K11,K12,K8_1,K8_2,K9 high) ===")
print(cmd(ser, f"doutall {MASK_HEX}"))

print("=== getdout after ===")
print(cmd(ser, "getdout"))

ser.close()
print("Done. 0x1F800000 afterwards means software control works.")

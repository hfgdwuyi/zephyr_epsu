#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
psu_dfu.py — CiosZhong PSU 串口固件升级工具

通过 USART1 (PB14/PB15) 把签名固件 (zephyr.signed.bin) 发送给运行中的 APP，
APP 写入 slot1 并请求 MCUboot swap，复位后自动运行新固件。

协议（与 uart_cmd.c dfu 一致）：
  dfu            → APP 擦除 slot1 并进入升级状态
  size <hex>     → 声明固件总字节数
  data <hex...>  → 每行最多 512 字节的 hex 数据
  全部发送后 APP 自动 boot_request_upgrade + 复位

用法：
  python3 psu_dfu.py /dev/cu.usbserial-XXX zephyr.signed.bin
依赖：pyserial
"""
import sys
import time
import serial

BLOCK = 512          # 每行数据字节数
HEX_PER_LINE = BLOCK * 2


def read_until_ack(ser, timeout=5):
    """读串口直到出现 'ACK ' 或 'ERR'（DFU 应答行），返回收到的文本。"""
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


def main():
    if len(sys.argv) < 3:
        print("usage: psu_dfu.py <serial-port> <signed-firmware.bin>")
        sys.exit(1)

    port = sys.argv[1]
    fw = sys.argv[2]

    with open(fw, "rb") as f:
        image = f.read()
    total = len(image)
    print(f"固件: {fw} ({total} bytes)")

    ser = serial.Serial(port, 115200, timeout=0.1)
    time.sleep(0.2)

    # 1. 进入 dfu
    ser.write(b"dfu\r\n")
    resp = read_until(ser, "size <hex>", 30)
    print(f"[DFU] {resp.strip()}")
    if "size <hex>" not in resp:
        print("!! APP 未进入 DFU 模式")
        sys.exit(1)

    # 2. 声明大小
    ser.write(f"size {total:X}\r\n".encode())
    resp = read_until(ser, "data <hex>", 10)
    print(f"[SIZE] {resp.strip()}")

    # 3. 分块发送，逐块等 ACK <off>；ERR/超时重发同一块。
    #    最后一块发完固件直接 "DFU done ... rebooting"（没有 ACK），
    #    若等 ACK 会超时并把重发打到已重启的新固件 → 误判失败。
    sent = 0
    total_blocks = (total + BLOCK - 1) // BLOCK
    for idx in range(total_blocks):
        off = idx * BLOCK
        chunk = image[off:off + BLOCK]
        last = (idx == total_blocks - 1)
        if last:
            ser.write(f"data {off:X} ".encode() + chunk.hex().encode() + b"\r\n")
            resp = read_until(ser, "rebooting", 30)
            if "rebooting" in resp:
                sent += len(chunk)
                print(f"  sent {sent}/{total}")
                print(f"[DONE] {resp.strip()}")
                ser.close()
                print("固件已上传，设备正在复位并由 MCUboot 完成升级...")
                return
            print(f"!! 末块无 rebooting 响应: {resp.strip()}")
            ser.close()
            sys.exit(1)

        ok = False
        for attempt in range(50):
            ser.write(f"data {off:X} ".encode() + chunk.hex().encode() + b"\r\n")
            resp = read_until_ack(ser, 5)
            if f"ACK {off + len(chunk):X}" in resp:
                sent += len(chunk)
                ok = True
                break
            # ERR len / ERR dfu write / 超时：清输入后重发本块（会话未销毁）
            ser.reset_input_buffer()
            if attempt == 49:
                print(f"!! 块 off=0x{off:X} 最后一次响应: {resp.strip()}")
        if not ok:
            print(f"!! 块 off=0x{off:X} 多次失败，放弃")
            ser.close()
            sys.exit(1)
        if sent % 8192 == 0 or sent >= total:
            print(f"  sent {sent}/{total}")

    # 4. 等 APP 完成并复位
    resp = read_until(ser, "rebooting", 30)
    print(f"[DONE] {resp.strip()}")
    ser.close()
    print("固件已上传，设备正在复位并由 MCUboot 完成升级...")


if __name__ == "__main__":
    main()

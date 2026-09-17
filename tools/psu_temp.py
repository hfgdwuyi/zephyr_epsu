#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
psu_temp.py - read the on-board TMP75 (USART1 command channel)

Hardware: TMP75 on I2C1 (SCL=PB8 / SDA=PB9), 7-bit address 0x48
Protocol: send `temp\\r\\n`, the firmware replies
          `temp=<v>.<vvv> C (raw_ok, errs=N)`

Usage:
  python3 psu_temp.py /dev/cu.usbserial-XXXX            # read once
  python3 psu_temp.py /dev/cu.usbserial-XXXX --count 0  # poll until Ctrl-C
  python3 psu_temp.py /dev/cu.usbserial-XXXX --count 20 --interval 0.5

Requires pyserial
"""
import argparse
import sys
import time

try:
    import serial
except ImportError:
    print("error: pyserial required (pip install pyserial)")
    sys.exit(1)

BAUD = 115200


def read_once(ser, timeout=2.0, verbose=False):
    """Send one temp command; return the reply text (None on timeout)."""
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
    ap = argparse.ArgumentParser(description="Read the on-board TMP75")
    ap.add_argument("port", help="serial device, e.g. /dev/cu.usbserial-XXXX")
    ap.add_argument("--count", type=int, default=1,
                    help="number of reads; 0 = loop forever (default 1)")
    ap.add_argument("--interval", type=float, default=1.0,
                    help="interval in seconds when looping (default 1.0)")
    ap.add_argument("--verbose", action="store_true", help="print the raw reply")
    args = ap.parse_args()

    try:
        ser = serial.Serial(args.port, BAUD, timeout=1)
    except Exception as exc:
        print(f"error: cannot open the serial port: {exc}")
        sys.exit(1)

    print(f"TMP75 read @ {args.port} ({BAUD} baud)  Ctrl-C to quit")

    n = 0
    try:
        while args.count == 0 or n < args.count:
            n += 1
            resp = read_once(ser, verbose=args.verbose)

            if resp is None:
                print(f"[{n:4d}] timeout (is the app running? USART1 PB14/PB15?)")
            elif resp.startswith("temp="):
                # normal: temp=25.375 C (raw_ok, errs=0)
                value = resp.split()[0].split("=", 1)[1]
                errs = ""
                if "errs=" in resp:
                    errs = " errs=" + resp.split("errs=")[1].rstrip(")")
                print(f"[{n:4d}] {value} °C{errs}")
            elif "ERR" in resp:
                print(f"[{n:4d}] firmware error: {resp}")
            else:
                print(f"[{n:4d}] unrecognised reply: {resp!r}")

            if args.count == 0 or n < args.count:
                time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nstopped.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()

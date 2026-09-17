#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
psu_monitor.py - CiosZhong PSU ADC channel monitor (command line)

Passively parses the SENSOR lines pushed by the firmware and redraws a table
in the terminal. No GUI, so it suits scripting / remote verification.

Usage:
  python3 psu_monitor.py                     # auto-detect the port
  python3 psu_monitor.py /dev/cu.usbserial-130
  python3 psu_monitor.py /dev/cu.usbserial-130 --raw   # also print raw lines

Requires pyserial
"""
import argparse
import re
import sys
import time

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("error: pyserial required")
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
    out = ["\033[H\033[J"]   # clear screen
    out.append("CiosZhong PSU - ADC channel monitor")
    out.append("=" * 52)
    out.append(f"{'Channel':<16}{'Pin':<8}{'Value':>14}{'':<3}{'Updated':<10}")
    out.append("-" * 52)
    for name, pin, unit in CHANNELS:
        v, ts = values.get(name, ("—", ""))
        out.append(f"{name:<16}{pin:<8}{v:>14}  {unit:<3}{ts:<10}")
    out.append("-" * 52)
    out.append(status)
    out.append("")
    out.append(f"{rounds} rounds received    Ctrl-C to quit")
    print("\n".join(out), flush=True)


def main():
    ap = argparse.ArgumentParser(description="ADC channel monitor")
    ap.add_argument("port", nargs="?")
    ap.add_argument("--raw", action="store_true", help="also print raw serial lines")
    args = ap.parse_args()

    port = args.port
    if not port:
        ports = [p.device for p in list_ports.comports()
                 if "usbserial" in p.device or "USB" in p.device or "SLAB" in p.device]
        port = ports[0] if ports else None
    if not port:
        print("error: no serial port found, specify one")
        sys.exit(1)

    ser = serial.Serial(port, BAUD, timeout=0.3)
    print(f"connected {port} @ {BAUD}, waiting for firmware data...")

    values = {}
    rounds = 0
    n_lines = 0
    last_rx = time.time()
    render(values, rounds, f"waiting for data... ({port})")

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
            # redraw after each complete round
            if n_lines >= len(CHANNELS):
                rounds += n_lines // len(CHANNELS)
                n_lines = n_lines % len(CHANNELS)
                render(values, rounds,
                       f"connected {port} | last rx {time.strftime('%H:%M:%S', time.localtime(last_rx))}")
            elif time.time() - last_rx > 3:
                render(values, rounds, f"! no data for {int(time.time()-last_rx)}s (is the board running? right port?)")
                last_rx = time.time()
    except KeyboardInterrupt:
        print("\nstopped.")
    finally:
        ser.close()


if __name__ == "__main__":
    main()

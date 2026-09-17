#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
psu_adc_monitor.py - CiosZhong PSU ADC channel monitor (tkinter GUI)

Features:
  - auto-detects the serial port (/dev/cu.usbserial-* or /dev/ttyUSB*)
  - passively parses the SENSOR lines pushed by the firmware
  - shows value, unit and update time for all 15 ADC channels
  - raw serial traffic is shown at the bottom
  - can send diagnostic commands (ain raw / temp / i2cscan)

Usage:
  python3 psu_adc_monitor.py                 # auto-detect the port
  python3 psu_adc_monitor.py /dev/cu.usbserial-130

Requires pyserial (tkinter ships with Python)
"""
import re
import sys
import threading
import time
import tkinter as tk
from tkinter import ttk

try:
    import serial
    from serial.tools import list_ports
except ImportError:
    print("error: pyserial required (pip install pyserial)")
    sys.exit(1)

BAUD = 115200

# Firmware report table (fixed order; missing values show '-')
CHANNELS = [
    ("temp1",          "°C"),
    ("temp2",          "°C"),
    ("adc_12v",        "V"),
    ("adc_pdc7",       "V"),
    ("adc_pdc6",       "V"),
    ("adc_pdc5",       "V"),
    ("adc_5v0",        "V"),
    ("adc_pdc0",       "V"),
    ("adc_pdc4",       "V"),
    ("adc_pdc2",       "V"),
    ("adc_pdc3",       "V"),
    ("adc_3v3",        "V"),
    ("adc_pdc1",       "V"),
    ("adc_pdc0_alt",   "V"),
    ("adc_vin",        "V"),
]

# Pin labels for cross-checking against hardware
PIN_HINT = {
    "temp1": "PA3", "temp2": "PA4",
    "adc_12v": "PH2", "adc_pdc7": "PF11", "adc_pdc6": "PF12", "adc_pdc5": "PF13",
    "adc_5v0": "PF14", "adc_pdc0": "PA6", "adc_pdc4": "PA0_C", "adc_pdc2": "PB0",
    "adc_pdc3": "PB1", "adc_3v3": "PC0", "adc_pdc1": "PC2",
    "adc_pdc0_alt": "PC3_C", "adc_vin": "PC2_C",
}

# SENSOR <name>: <value...>
RE_SENSOR = re.compile(r"SENSOR\s+(\w+)\s*:\s*(.+?)\s*$")


def find_ports():
    return [p.device for p in list_ports.comports()]


class MonitorApp:
    def __init__(self, root, port=None):
        self.root = root
        self.ser = None
        self.rx_thread = None
        self.running = False
        self.frames = 0
        self.lines_seen = 0
        self.last_rx = 0.0
        self.cells = {}

        root.title("CiosZhong PSU - ADC channel monitor")
        root.geometry("560x700")

        # ---------- top: serial control ----------
        top = ttk.Frame(root, padding=8)
        top.pack(fill="x")

        ttk.Label(top, text="Port:").pack(side="left")
        self.port_var = tk.StringVar(value=port or "")
        self.port_box = ttk.Combobox(top, textvariable=self.port_var, width=26)
        self.port_box["values"] = find_ports()
        self.port_box.pack(side="left", padx=4)

        ttk.Button(top, text="Refresh", width=8, command=self.refresh_ports).pack(side="left")
        self.btn = ttk.Button(top, text="Connect", width=8, command=self.toggle)
        self.btn.pack(side="left", padx=4)

        # ---------- middle: channel table ----------
        mid = ttk.LabelFrame(root, text="ADC channels (one round per second)", padding=6)
        mid.pack(fill="x", padx=8, pady=4)

        hdr = ttk.Frame(mid)
        hdr.pack(fill="x")
        for txt, w in (("Channel", 14), ("Pin", 8), ("Value", 12), ("Unit", 5), ("Updated", 10)):
            ttk.Label(hdr, text=txt, width=w, font=("Helvetica", 11, "bold"),
                      anchor="w").pack(side="left")

        for name, unit in CHANNELS:
            row = ttk.Frame(mid)
            row.pack(fill="x", pady=1)
            ttk.Label(row, text=name, width=14, anchor="w").pack(side="left")
            ttk.Label(row, text=PIN_HINT.get(name, ""), width=8,
                      anchor="w", foreground="#666").pack(side="left")
            val = ttk.Label(row, text="—", width=12, anchor="w")
            val.pack(side="left")
            ttk.Label(row, text=unit, width=5, anchor="w").pack(side="left")
            tst = ttk.Label(row, text="", width=8, anchor="w", foreground="#888")
            tst.pack(side="left")
            self.cells[name] = (val, tst)

        # ---------- manual command ----------
        cmd = ttk.Frame(root, padding=(8, 2))
        cmd.pack(fill="x")
        ttk.Label(cmd, text="Command:").pack(side="left")
        self.cmd_var = tk.StringVar(value="ain raw")
        ent = ttk.Entry(cmd, textvariable=self.cmd_var, width=24)
        ent.pack(side="left", padx=4)
        ent.bind("<Return>", lambda e: self.send_cmd())
        ttk.Button(cmd, text="Send", width=6, command=self.send_cmd).pack(side="left")
        for quick in ("ain", "ain raw", "temp", "i2cscan", "info"):
            ttk.Button(cmd, text=quick, width=7,
                       command=lambda q=quick: self.quick(q)).pack(side="left", padx=1)

        # ---------- raw traffic ----------
        raw_box = ttk.LabelFrame(root, text="Raw serial data", padding=4)
        raw_box.pack(fill="both", expand=True, padx=8, pady=4)
        self.raw = tk.Text(raw_box, height=12, wrap="none",
                           font=("Menlo", 10), background="#101418", foreground="#c8e6c9")
        self.raw.pack(fill="both", expand=True)

        # ---------- status bar ----------
        self.status = tk.StringVar(value="not connected")
        ttk.Label(root, textvariable=self.status, anchor="w",
                  relief="sunken", padding=4).pack(fill="x", side="bottom")

        self.refresh_ports()

    # ------------------------------------------------------------------
    def refresh_ports(self):
        ports = find_ports()
        self.port_box["values"] = ports
        if ports and not self.port_var.get():
            self.port_var.set(ports[0])

    def quick(self, q):
        self.cmd_var.set(q)
        self.send_cmd()

    def send_cmd(self):
        if not self.ser:
            return
        cmd = self.cmd_var.get().strip()
        if not cmd:
            return
        try:
            self.ser.write((cmd + "\r\n").encode())
            self.append_raw(f">>> {cmd}")
        except Exception as exc:
            self.append_raw(f"!!! send failed: {exc}")

    def toggle(self):
        if self.ser:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        port = self.port_var.get().strip()
        if not port:
            self.status.set("select a port first")
            return
        try:
            self.ser = serial.Serial(port, BAUD, timeout=0.2)
        except Exception as exc:
            self.status.set(f"open failed: {exc}")
            return
        self.running = True
        self.rx_thread = threading.Thread(target=self.rx_loop, daemon=True)
        self.rx_thread.start()
        self.btn.config(text="Disconnect")
        self.status.set(f"connected {port} @ {BAUD}")

    def disconnect(self):
        self.running = False
        if self.rx_thread:
            self.rx_thread.join(timeout=1)
        if self.ser:
            try:
                self.ser.close()
            except Exception:
                pass
        self.ser = None
        self.btn.config(text="Connect")
        self.status.set("not connected")

    # ------------------------------------------------------------------
    def rx_loop(self):
        buf = b""
        while self.running:
            try:
                data = self.ser.read(256)
            except Exception as exc:
                self.status.set(f"read error: {exc}")
                break
            if not data:
                continue
            self.last_rx = time.time()
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                text = line.decode(errors="replace").rstrip("\r")
                if text.strip():
                    self.root.after(0, self.handle_line, text)

    def handle_line(self, text):
        self.lines_seen += 1
        self.frames = self.lines_seen // len(CHANNELS)
        self.append_raw(text)

        m = RE_SENSOR.match(text)
        if m:
            name, val = m.group(1), m.group(2)
            if name in self.cells:
                lbl, tst = self.cells[name]
                # value and unit are shown separately
                num = val
                unit = ""
                if val.endswith("°C"):
                    num, unit = val[:-2].strip(), "°C"
                elif " V @" in val:
                    num, unit = val.split(" V @")[0].strip(), "V"
                elif val.endswith(" V"):
                    num, unit = val[:-2].strip(), "V"
                elif val.startswith("n/a") or val.startswith("FAULT"):
                    num, unit = val, ""
                lbl.config(text=num)
                tst.config(text=time.strftime("%H:%M:%S"))

        self.status.set(
            f"connected | lines {self.lines_seen} | ~{self.frames} rounds | "
            f"last rx {time.strftime('%H:%M:%S', time.localtime(self.last_rx))}")

    def append_raw(self, text):
        self.raw.insert("end", text + "\n")
        # keep only the last 400 lines
        if int(self.raw.index("end-1c").split(".")[0]) > 400:
            self.raw.delete("1.0", "100.0")
        self.raw.see("end")


def main():
    port = sys.argv[1] if len(sys.argv) > 1 else None
    port = port or (find_ports()[0] if find_ports() else None)
    root = tk.Tk()
    app = MonitorApp(root, port)
    if port:
        app.connect()
    root.protocol("WM_DELETE_WINDOW", lambda: (app.disconnect(), root.destroy()))
    root.mainloop()


if __name__ == "__main__":
    main()

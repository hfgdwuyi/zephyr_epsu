#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
psu_dfu_gui.py - CiosZhong PSU serial firmware upgrade GUI

Features:
  - drag a .signed.bin onto the window, or use the "Select firmware" button
  - pick the serial port (auto-scanned) and baud rate
  - one-click upgrade: dfu -> size -> data (per-block ACK) -> reboot
  - live log

Usage:
  .venv/bin/python3 tools/psu_dfu_gui.py
  or drop a firmware onto the script icon (macOS passes the path as argv)
  Requires pyserial and tkinterdnd2
"""
import os
import sys
import time
import threading

import serial

try:
    from tkinterdnd2 import DND_FILES, TkinterDnD
    HAS_DND = True
except Exception:
    HAS_DND = False

import tkinter as tk
from tkinter import ttk, filedialog, messagebox

BLOCK = 512  # data bytes per line (matches uart_cmd.c DFU_BLOCK_MAX)


def list_serial_ports():
    """Return the list of available serial ports."""
    ports = []
    if sys.platform == "darwin":
        base = "/dev"
        for name in sorted(os.listdir(base)):
            if name.startswith("cu."):
                ports.append(os.path.join(base, name))
    else:
        try:
            from serial.tools import list_ports
            ports = [p.device for p in list_ports.comports()]
        except Exception:
            pass
    return ports


class DfuGUI:
    def __init__(self, root, initial_file=None):
        self.root = root
        root.title("CiosZhong PSU serial firmware upgrade")
        root.geometry("720x520")
        root.minsize(600, 420)

        self.fw_path = tk.StringVar(value=initial_file or "")
        self.port = tk.StringVar()
        self.baud = tk.IntVar(value=115200)
        self.running = False

        self._build_ui()
        self._refresh_ports()

        if HAS_DND:
            try:
                root.drop_target_register(DND_FILES)
                root.dnd_bind("<<Drop>>", self._on_drop)
                self.log("Drag and drop: drop a .signed.bin onto the window")
            except Exception as e:
                self.log(f"drag-and-drop init failed (use the button): {e}")
        if initial_file:
            self.log(f"firmware loaded: {os.path.basename(initial_file)}")

    # ---------------- UI ----------------
    def _build_ui(self):
        pad = {"padx": 8, "pady": 4}

        fw = ttk.LabelFrame(self.root, text="1. Firmware")
        fw.pack(fill="x", **pad)
        row = ttk.Frame(fw)
        row.pack(fill="x", padx=6, pady=6)
        self.fw_entry = ttk.Entry(row, textvariable=self.fw_path)
        self.fw_entry.pack(side="left", fill="x", expand=True)
        ttk.Button(row, text="Select firmware...", command=self._choose_fw).pack(side="left", padx=4)
        ttk.Button(row, text="Read version", command=self._read_version).pack(side="left")

        port = ttk.LabelFrame(self.root, text="2. Serial port")
        port.pack(fill="x", **pad)
        row2 = ttk.Frame(port)
        row2.pack(fill="x", padx=6, pady=6)
        self.port_combo = ttk.Combobox(row2, textvariable=self.port, width=24)
        self.port_combo.pack(side="left")
        ttk.Button(row2, text="Refresh", command=self._refresh_ports).pack(side="left", padx=4)
        ttk.Label(row2, text="Baud:").pack(side="left", padx=(16, 4))
        ttk.Combobox(row2, textvariable=self.baud, width=8,
                     values=[9600, 19200, 38400, 57600, 115200, 230400]).pack(side="left")

        act = ttk.LabelFrame(self.root, text="3. Upgrade")
        act.pack(fill="x", **pad)
        row3 = ttk.Frame(act)
        row3.pack(fill="x", padx=6, pady=6)
        self.btn_upgrade = ttk.Button(row3, text="Start upgrade", command=self._start_upgrade)
        self.btn_upgrade.pack(side="left")
        self.btn_cancel = ttk.Button(row3, text="Cancel", command=self._cancel, state="disabled")
        self.btn_cancel.pack(side="left", padx=6)
        self.progress = ttk.Progressbar(row3, mode="determinate")
        self.progress.pack(side="left", fill="x", expand=True, padx=8)

        logf = ttk.LabelFrame(self.root, text="Log")
        logf.pack(fill="both", expand=True, **pad)
        self.log_text = tk.Text(logf, height=14, state="disabled", wrap="word",
                                font=("Menlo", 10))
        sb = ttk.Scrollbar(logf, command=self.log_text.yview)
        self.log_text.configure(yscrollcommand=sb.set)
        sb.pack(side="right", fill="y")
        self.log_text.pack(side="left", fill="both", expand=True)

    def _refresh_ports(self):
        ports = list_serial_ports()
        self.port_combo["values"] = ports
        if ports:
            if self.port.get() not in ports:
                self.port.set(ports[0])
        else:
            self.port.set("")

    def _choose_fw(self):
        path = filedialog.askopenfilename(
            title="Select signed firmware (.signed.bin)",
            filetypes=[("Signed firmware", "*.bin"), ("All files", "*.*")],
            initialdir=os.path.dirname(self.fw_path.get()) if self.fw_path.get() else None,
        )
        if path:
            self.fw_path.set(path)
            self.log(f"selected: {path}")

    def _on_drop(self, event):
        # tkinterdnd2 passes a brace-wrapped path list
        files = self.root.tk.splitlist(event.data)
        for f in files:
            if f.lower().endswith(".bin") or os.path.isfile(f):
                self.fw_path.set(f)
                self.log(f"dropped firmware: {os.path.basename(f)}")
                return
        self.log("dropped file is not a .bin firmware")

    # ---------------- helpers ----------------
    def log(self, msg):
        def _do():
            self.log_text.configure(state="normal")
            self.log_text.insert("end", msg + "\n")
            self.log_text.see("end")
            self.log_text.configure(state="disabled")
        self.root.after(0, _do)

    def set_busy(self, busy):
        self.running = busy
        self.btn_upgrade.configure(state="disabled" if busy else "normal")
        self.btn_cancel.configure(state="normal" if busy else "disabled")

    # ---------------- firmware version ----------------
    def _read_version(self):
        path = self.fw_path.get()
        if not path or not os.path.isfile(path):
            messagebox.showwarning("Notice", "Select a firmware file first")
            return
        try:
            with open(path, "rb") as f:
                data = f.read(0x100)
            import struct
            # imgtool header: magic@0, version at offset 0x30 (48), 4x u32
            if len(data) >= 0x40:
                major, minor, rev, build = struct.unpack("<4I", data[0x30:0x40])
                self.log(f"{os.path.basename(path)}: version {major}.{minor}.{rev}+{build}, "
                         f"{os.path.getsize(path)} bytes")
            else:
                self.log(f"{os.path.basename(path)}: {os.path.getsize(path)} bytes")
        except Exception as e:
            self.log(f"cannot read the version: {e}")

    # ---------------- upgrade core ----------------
    def _cancel(self):
        self._cancel_flag = True

    def _start_upgrade(self):
        path = self.fw_path.get()
        port = self.port.get()
        if not path or not os.path.isfile(path):
            messagebox.showwarning("Notice", "Select a firmware file first")
            return
        if not port:
            messagebox.showwarning("Notice", "Select a serial port")
            return
        self.set_busy(True)
        self._cancel_flag = False
        self.progress.configure(value=0)
        threading.Thread(target=self._upgrade_worker, args=(port, path), daemon=True).start()

    def _read_until_ack(self, ser, timeout=5.0):
        buf = b""
        end = time.time() + timeout
        while time.time() < end:
            if self._cancel_flag:
                return buf.decode(errors="replace")
            if ser.in_waiting:
                data = ser.read(ser.in_waiting)
                buf += data
                if b"ACK " in buf or b"ERR" in buf:
                    return buf.decode(errors="replace")
            time.sleep(0.01)
        return buf.decode(errors="replace")

    def _read_until(self, ser, token, timeout=10.0):
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

    def _upgrade_worker(self, port, fw):
        try:
            with open(fw, "rb") as f:
                image = f.read()
            total = len(image)
            self.log(f"[start] firmware {os.path.basename(fw)} ({total} bytes) -> {port}")

            ser = serial.Serial(port, self.baud.get(), timeout=0.1)
            time.sleep(0.3)

            # 1. enter dfu
            ser.write(b"\r\n\r\n")
            time.sleep(0.1)
            ser.reset_input_buffer()
            ser.write(b"dfu\r\n")
            resp = self._read_until(ser, "size <hex>", 30)
            self.log(f"[DFU] {resp.strip()}")
            if "size <hex>" not in resp:
                raise RuntimeError("app did not enter DFU mode")

            # 2. size
            ser.write(f"size {total:X}\r\n".encode())
            resp = self._read_until(ser, "data <hex>", 10)
            self.log(f"[SIZE] {resp.strip()}")

            # 3. send the blocks
            total_blocks = (total + BLOCK - 1) // BLOCK
            sent = 0
            for idx in range(total_blocks):
                if self._cancel_flag:
                    self.log("[cancel] cancelled by the user")
                    ser.close()
                    return
                off = idx * BLOCK
                chunk = image[off:off + BLOCK]
                last = (idx == total_blocks - 1)

                if last:
                    ser.write(f"data {off:X} ".encode() + chunk.hex().encode() + b"\r\n")
                    resp = self._read_until(ser, "rebooting", 30)
                    if "rebooting" in resp:
                        sent += len(chunk)
                        self.progress.configure(value=100)
                        self.log(f"[done] {resp.strip()}")
                        self.log("Upload complete; the device is resetting.")
                        ser.close()
                        return
                    self.log(f"[no response to last block] {resp.strip()}")
                    ser.close()
                    return

                ok = False
                for attempt in range(50):
                    if self._cancel_flag:
                        ser.close()
                        self.log("[cancel]")
                        return
                    ser.write(f"data {off:X} ".encode() + chunk.hex().encode() + b"\r\n")
                    resp = self._read_until_ack(ser, 5)
                    if f"ACK {off + len(chunk):X}" in resp:
                        sent += len(chunk)
                        ok = True
                        break
                    ser.reset_input_buffer()
                if not ok:
                    raise RuntimeError(f"block 0x{off:X} failed repeatedly")

                self.progress.configure(value=int(sent * 100 / total))
                if sent % 16384 == 0 or sent >= total:
                    self.log(f"  sent {sent}/{total}")

            ser.close()
        except Exception as e:
            self.log(f"[error] {e}")
            messagebox.showerror("Upgrade failed", str(e))
        finally:
            self.root.after(0, lambda: self.set_busy(False))


def main():
    initial = None
    if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]):
        initial = sys.argv[1]

    if HAS_DND:
        root = TkinterDnD.Tk()
    else:
        root = tk.Tk()
    app = DfuGUI(root, initial_file=initial)
    app.log("Ready. Select/drop a .signed.bin, pick a port, click Start.")
    root.mainloop()


if __name__ == "__main__":
    main()

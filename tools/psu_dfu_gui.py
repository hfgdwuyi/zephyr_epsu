#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
psu_dfu_gui.py — CiosZhong PSU 串口固件升级上位机（图形界面）

支持：
  - 拖拽固件文件（.signed.bin）到窗口，或点"选择固件"
  - 选择串口（自动扫描）与波特率
  - 一键升级：dfu → size → data(逐块 ACK) → reboot → MCUboot swap
  - 实时日志显示

用法：
  .venv/bin/python3 tools/psu_dfu_gui.py
  或双击/拖固件到脚本图标（macOS 会把文件路径作 argv 传入）
  依赖：pyserial, tkinterdnd2
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

BLOCK = 512  # 每行数据字节数（与固件 uart_cmd.c DFU_BLOCK_MAX 一致）


def list_serial_ports():
    """返回系统可用串口名列表。"""
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
        root.title("CiosZhong PSU 串口固件升级工具")
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
                self.log("支持拖拽：把 .signed.bin 文件拖到窗口即可")
            except Exception as e:
                self.log(f"拖放初始化失败（可用“选择固件”按钮）: {e}")
        if initial_file:
            self.log(f"已加载固件: {os.path.basename(initial_file)}")

    # ---------------- UI ----------------
    def _build_ui(self):
        pad = {"padx": 8, "pady": 4}

        fw = ttk.LabelFrame(self.root, text="1. 固件文件")
        fw.pack(fill="x", **pad)
        row = ttk.Frame(fw)
        row.pack(fill="x", padx=6, pady=6)
        self.fw_entry = ttk.Entry(row, textvariable=self.fw_path)
        self.fw_entry.pack(side="left", fill="x", expand=True)
        ttk.Button(row, text="选择固件…", command=self._choose_fw).pack(side="left", padx=4)
        ttk.Button(row, text="读取版本", command=self._read_version).pack(side="left")

        port = ttk.LabelFrame(self.root, text="2. 串口")
        port.pack(fill="x", **pad)
        row2 = ttk.Frame(port)
        row2.pack(fill="x", padx=6, pady=6)
        self.port_combo = ttk.Combobox(row2, textvariable=self.port, width=24)
        self.port_combo.pack(side="left")
        ttk.Button(row2, text="刷新", command=self._refresh_ports).pack(side="left", padx=4)
        ttk.Label(row2, text="波特率:").pack(side="left", padx=(16, 4))
        ttk.Combobox(row2, textvariable=self.baud, width=8,
                     values=[9600, 19200, 38400, 57600, 115200, 230400]).pack(side="left")

        act = ttk.LabelFrame(self.root, text="3. 升级")
        act.pack(fill="x", **pad)
        row3 = ttk.Frame(act)
        row3.pack(fill="x", padx=6, pady=6)
        self.btn_upgrade = ttk.Button(row3, text="开始升级", command=self._start_upgrade)
        self.btn_upgrade.pack(side="left")
        self.btn_cancel = ttk.Button(row3, text="取消", command=self._cancel, state="disabled")
        self.btn_cancel.pack(side="left", padx=6)
        self.progress = ttk.Progressbar(row3, mode="determinate")
        self.progress.pack(side="left", fill="x", expand=True, padx=8)

        logf = ttk.LabelFrame(self.root, text="日志")
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
            title="选择签名固件 (.signed.bin)",
            filetypes=[("Signed firmware", "*.bin"), ("All files", "*.*")],
            initialdir=os.path.dirname(self.fw_path.get()) if self.fw_path.get() else None,
        )
        if path:
            self.fw_path.set(path)
            self.log(f"已选择: {path}")

    def _on_drop(self, event):
        # tkinterdnd2 传入的是带花括号路径列表
        files = self.root.tk.splitlist(event.data)
        for f in files:
            if f.lower().endswith(".bin") or os.path.isfile(f):
                self.fw_path.set(f)
                self.log(f"拖入固件: {os.path.basename(f)}")
                return
        self.log("拖入的文件不是固件 .bin")

    # ---------------- 辅助 ----------------
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

    # ---------------- 固件版本读取 ----------------
    def _read_version(self):
        path = self.fw_path.get()
        if not path or not os.path.isfile(path):
            messagebox.showwarning("提示", "请先选择固件文件")
            return
        try:
            with open(path, "rb") as f:
                data = f.read(0x100)
            import struct
            # imgtool header: magic@0, version at offset 0x30 (48), 4x u32
            if len(data) >= 0x40:
                major, minor, rev, build = struct.unpack("<4I", data[0x30:0x40])
                self.log(f"{os.path.basename(path)}: 版本 {major}.{minor}.{rev}+{build}, "
                         f"{os.path.getsize(path)} bytes")
            else:
                self.log(f"{os.path.basename(path)}: {os.path.getsize(path)} bytes")
        except Exception as e:
            self.log(f"读取版本失败: {e}")

    # ---------------- 升级核心 ----------------
    def _cancel(self):
        self._cancel_flag = True

    def _start_upgrade(self):
        path = self.fw_path.get()
        port = self.port.get()
        if not path or not os.path.isfile(path):
            messagebox.showwarning("提示", "请先选择固件文件")
            return
        if not port:
            messagebox.showwarning("提示", "请选择串口")
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
            self.log(f"[开始] 固件 {os.path.basename(fw)} ({total} bytes) → {port}")

            ser = serial.Serial(port, self.baud.get(), timeout=0.1)
            time.sleep(0.3)

            # 1. 进入 dfu
            ser.write(b"\r\n\r\n")
            time.sleep(0.1)
            ser.reset_input_buffer()
            ser.write(b"dfu\r\n")
            resp = self._read_until(ser, "size <hex>", 30)
            self.log(f"[DFU] {resp.strip()}")
            if "size <hex>" not in resp:
                raise RuntimeError("APP 未进入 DFU 模式")

            # 2. size
            ser.write(f"size {total:X}\r\n".encode())
            resp = self._read_until(ser, "data <hex>", 10)
            self.log(f"[SIZE] {resp.strip()}")

            # 3. 分块发送
            total_blocks = (total + BLOCK - 1) // BLOCK
            sent = 0
            for idx in range(total_blocks):
                if self._cancel_flag:
                    self.log("[取消] 用户取消")
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
                        self.log(f"[完成] {resp.strip()}")
                        self.log("固件已上传，设备复位中，MCUboot 将完成 swap…")
                        ser.close()
                        return
                    self.log(f"[末块无响应] {resp.strip()}")
                    ser.close()
                    return

                ok = False
                for attempt in range(50):
                    if self._cancel_flag:
                        ser.close()
                        self.log("[取消]")
                        return
                    ser.write(f"data {off:X} ".encode() + chunk.hex().encode() + b"\r\n")
                    resp = self._read_until_ack(ser, 5)
                    if f"ACK {off + len(chunk):X}" in resp:
                        sent += len(chunk)
                        ok = True
                        break
                    ser.reset_input_buffer()
                if not ok:
                    raise RuntimeError(f"块 0x{off:X} 多次失败")

                self.progress.configure(value=int(sent * 100 / total))
                if sent % 16384 == 0 or sent >= total:
                    self.log(f"  已发送 {sent}/{total}")

            ser.close()
        except Exception as e:
            self.log(f"[错误] {e}")
            messagebox.showerror("升级失败", str(e))
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
    app.log("就绪。选择/拖入 .signed.bin，选串口，点“开始升级”。")
    root.mainloop()


if __name__ == "__main__":
    main()

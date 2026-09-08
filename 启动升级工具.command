#!/bin/sh
# 启动 PSU 串口升级 GUI（macOS）
# 双击运行；或把 .signed.bin 文件拖到本脚本图标上自动加载。
cd "$(dirname "$0")"
if [ -n "$1" ]; then
    exec .venv/bin/python3 tools/psu_dfu_gui.py "$1"
else
    exec .venv/bin/python3 tools/psu_dfu_gui.py
fi

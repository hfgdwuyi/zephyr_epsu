#!/bin/sh
# Launch the PSU serial upgrade GUI (macOS)
# Double-click to run, or drop a .signed.bin onto this script to load it.
cd "$(dirname "$0")"
if [ -n "$1" ]; then
    exec .venv/bin/python3 tools/psu_dfu_gui.py "$1"
else
    exec .venv/bin/python3 tools/psu_dfu_gui.py
fi

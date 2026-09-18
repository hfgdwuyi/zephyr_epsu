#!/usr/bin/env python3
"""check_build_layout.py - verify the built bootloader/app agree on the layout.

A serial DFU can only ever erase slot1, but *which* address that is comes from
the app's devicetree, and MCUboot's idea of slot0/slot1 comes from its own
devicetree. If either build sees a different layout than expected, an upgrade
can erase the wrong region (in the worst case the bootloader it is running
from). Check both artifacts before flashing anything.

Usage: check_build_layout.py [--app <zephyr.dts>] [--boot <zephyr.dts>]
                             [--boot-config <build-mcuboot/zephyr/.config>]
"""
import argparse
import pathlib
import re
import sys

FLASH_BASE = 0x08000000

# name -> (offset relative to FLASH_BASE, size); the single source of truth is
# application/boards/arm/cioszhong_psu/cioszhong_psu_stm32h745xx_m7.dts
EXPECTED = {
    "boot_partition": (0x00000000, 0x00020000),
    "slot0_partition": (0x00020000, 0x00080000),
    "slot1_partition": (0x00100000, 0x00080000),
}

# MCUboot must copy slot1 over slot0; anything else invalidates the DFU offset
# (DFU_SECONDARY_IMG_OFFSET) and the transfer layout.
BOOT_CONFIG_REQUIRED = {
    "CONFIG_BOOT_UPGRADE_ONLY": "y",
}
BOOT_CONFIG_FORBIDDEN = (
    "CONFIG_BOOT_SWAP_USING_SCRATCH",
    "CONFIG_BOOT_SWAP_USING_OFFSET",
    "CONFIG_BOOT_SWAP_USING_MOVE",
    "CONFIG_BOOT_DIRECT_XIP",
)

NODE_RE = re.compile(
    r"(\w+_partition):\s*partition@[0-9a-fA-F]+\s*\{(?:(?!\};).)*?"
    r"reg\s*=\s*<\s*0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s*>",
    re.S,
)

# Every child of a `partitions` node, in devicetree order. `_PARTITION_ID` (what
# FIXED_PARTITION_ID() expands to) is assigned by this order, so a stray or
# duplicated node shifts every id after it - which is how slot1 can end up
# pointing at the 128 KB boot partition.
PART_RE = re.compile(
    r"(\w+):\s*partition@([0-9a-fA-F]+)\s*\{(?:(?!\};).)*?"
    r"reg\s*=\s*<\s*0x([0-9a-fA-F]+)\s+0x([0-9a-fA-F]+)\s*>",
    re.S,
)


def partitions_node(dts_path, flash_label):
    """Return the children of the `partitions` node under a flash node."""
    text = pathlib.Path(dts_path).read_text(encoding="utf-8", errors="replace")
    m = re.search(rf"{flash_label}\s*:\s*\w+@[0-9a-fA-F]+\s*\{{(.*?)\n\t\}};", text, re.S)
    if not m:
        return None
    block = m.group(1)
    m2 = re.search(r"partitions\s*\{(.*?)\n\t\t\};", block, re.S)
    if not m2:
        return None
    return PART_RE.findall(m2.group(1))


def report_table(path, label):
    """Print every partition of the internal flash with its generated id."""
    rows = partitions_node(path, "flash0")
    if rows is None:
        return
    print(f"  {label}: internal flash partition table (id order = _PARTITION_ID)")
    for idx, (lbl, unit, off, size) in enumerate(rows):
        mark = ""
        for name, (want_off, want_size) in EXPECTED.items():
            if lbl == name and (int(off, 16), int(size, 16)) == (want_off, want_size):
                mark = "  <- " + name
        print(f"    id {idx}: {lbl:16} @0x{int(off, 16):06X} size 0x{int(size, 16):X}{mark}")
    if len(rows) != len(EXPECTED):
        print(f"    WARNING: {len(rows)} nodes, expected {len(EXPECTED)} "
              f"(an extra node shifts every _PARTITION_ID after it)")


def parse_partitions(dts_path):
    text = pathlib.Path(dts_path).read_text(encoding="utf-8", errors="replace")
    found = {}
    for name, off, size in NODE_RE.findall(text):
        if name in EXPECTED:
            found[name] = (int(off, 16), int(size, 16))
    return found


def check_dts(path, label, errors):
    if not pathlib.Path(path).is_file():
        errors.append(f"{label}: {path} not found (build it first)")
        return
    parts = parse_partitions(path)
    if not parts:
        errors.append(f"{label}: no mcuboot partitions in {path}")
        return

    print(f"  {label} ({path}):")
    for name, (want_off, want_size) in EXPECTED.items():
        if name not in parts:
            errors.append(f"{label}: {name} missing")
            continue
        off, size = parts[name]
        ok = (off, size) == (want_off, want_size)
        print(f"    {'OK ' if ok else 'BAD'} {name:16} "
              f"@{FLASH_BASE + off:#010x} size {size:#x}"
              + ("" if ok else f"   expected @{FLASH_BASE + want_off:#010x} "
                                f"size {want_size:#x}"))
        if not ok:
            errors.append(f"{label}: {name} layout mismatch")

    # The upstream rule MCUboot relies on: the secondary slot must not overlap
    # the bootloader, otherwise an upgrade can erase the bootloader.
    if parts.get("slot1_partition", (0, 0))[0] < parts.get("boot_partition", (0, 0))[1]:
        errors.append(f"{label}: slot1 overlaps the bootloader region")


def check_boot_config(path, errors):
    if not pathlib.Path(path).is_file():
        errors.append(f"bootloader config {path} not found")
        return
    text = pathlib.Path(path).read_text(encoding="utf-8", errors="replace")
    print(f"  bootloader config ({path}):")
    for key, want in BOOT_CONFIG_REQUIRED.items():
        got = f"{key}=y" in text
        print(f"    {'OK ' if got else 'BAD'} {key}={want}")
        if not got:
            errors.append(f"bootloader: {key} is not y")
    for key in BOOT_CONFIG_FORBIDDEN:
        if f"{key}=y" in text:
            print(f"    BAD {key} must not be y")
            errors.append(f"bootloader: {key} must not be enabled")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", default="build/zephyr/zephyr.dts")
    ap.add_argument("--boot", default="build-mcuboot/zephyr/zephyr.dts")
    ap.add_argument("--boot-config", default="build-mcuboot/zephyr/.config")
    args = ap.parse_args()

    errors = []
    print("layout check (flash base 0x%08X):" % FLASH_BASE)
    check_dts(args.app, "app", errors)
    report_table(args.app, "app")
    check_dts(args.boot, "boot", errors)
    check_boot_config(args.boot_config, errors)

    if errors:
        print("\nFAILED:")
        for e in errors:
            print("  -", e)
        return 1
    print("\nOK: bootloader and app agree with the expected partition layout")
    return 0


if __name__ == "__main__":
    sys.exit(main())

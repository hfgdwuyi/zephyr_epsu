#!/usr/bin/env python3
"""check_signed_image.py - validate a signed MCUboot image before DFU.

The serial DFU path only accepts images that carry a valid MCUboot header, the
expected version and a payload that fits into slot0, so this is checked here
instead of being discovered on the board.

Usage: check_signed_image.py <image> <expected-version> [slot-size]
       (slot-size defaults to 0x80000 = 512 KB, matching slot0)
"""
import pathlib
import struct
import sys

MCUBOOT_MAGIC = 0x96F3B83D
HEADER_SIZE = 0x400


def main() -> int:
    if len(sys.argv) < 3:
        print(__doc__.strip())
        return 2

    path = pathlib.Path(sys.argv[1])
    want = sys.argv[2]
    slot = int(sys.argv[3], 0) if len(sys.argv) > 3 else 0x80000

    if not path.is_file():
        print(f"ERROR: {path} not found")
        return 1

    data = path.read_bytes()
    magic, _load, hdr, _ptlv, img, _flags = struct.unpack_from("<IIHHII", data, 0)
    major, minor, rev, build = struct.unpack_from("<BBHI", data, 20)

    if magic != MCUBOOT_MAGIC:
        print(f"ERROR: no MCUboot header in {path} (magic=0x{magic:08X})")
        return 1
    if hdr != HEADER_SIZE:
        print(f"ERROR: header size 0x{hdr:x}, expected 0x{HEADER_SIZE:x}")
        return 1

    version = f"{major}.{minor}.{rev}"
    if version != want:
        print(f"ERROR: image version {version} != {want} "
              f"(sync imgtool --version with CIOS_ZHONG_FW_VERSION)")
        return 1

    if len(data) > slot:
        print(f"ERROR: image {len(data):#x} does not fit slot0 ({slot:#x})")
        return 1

    print(f"OK  {path}: version={version}+{build} hdr={hdr:#x} "
          f"payload={img:#x} total={len(data):#x} (slot0 {slot:#x})")
    return 0


if __name__ == "__main__":
    sys.exit(main())

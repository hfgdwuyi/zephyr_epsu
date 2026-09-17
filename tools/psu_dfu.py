#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
psu_dfu.py - CiosZhong PSU serial firmware upgrade tool (repeat / reliability)

Sends the signed firmware (zephyr.signed.bin) over USART1 (PB14/PB15) to the
running app, which writes it to slot1 and requests the MCUboot upgrade.

Protocol (same as uart_cmd.c):
  dfu            -> app erases slot1 and enters upgrade mode
  size <hex>     -> total firmware size
  data <off> <hex...> -> 512 B hex block with offset; per-block ACK, retry
  after the last block the app calls boot_request_upgrade() and resets

Usage:
  once:  python3 psu_dfu.py <serial-port> <signed-firmware.bin>
  loop:  python3 psu_dfu.py <serial-port> <signed-firmware.bin> --count 50
  other: --stop-on-fail   stop at the first failure
         --interval SEC   extra wait between rounds (default 0)

Requires pyserial
"""
import argparse
import sys
import time

import serial

BLOCK = 512  # data bytes per line (matches uart_cmd.c DFU_BLOCK_MAX)


# --------------------------------------------------------------------------
# Serial read helpers
# --------------------------------------------------------------------------
def read_until_ack(ser, timeout=5):
    """Read until 'ACK ' or 'ERR' appears (a DFU response line)."""
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
    """Read until token appears; return everything received."""
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


# --------------------------------------------------------------------------
# Single upgrade
# --------------------------------------------------------------------------
def upgrade_once(ser, image, verbose=True):
    """Run one full DFU upload and trigger the reboot.

    Returns True on success (upload done + rebooting seen).
    The caller reopens the port afterwards to wait for the new firmware.
    """
    total = len(image)
    total_blocks = (total + BLOCK - 1) // BLOCK

    # 1. enter dfu (clear leftovers first for a clean handshake)
    ser.write(b"\r\n\r\n")
    time.sleep(0.1)
    ser.reset_input_buffer()
    ser.write(b"dfu\r\n")
    resp = read_until(ser, "size <hex>", 30)
    if "size <hex>" not in resp:
        print(f"  [FAIL] app did not enter DFU mode: {resp.strip()[:80]}")
        return False

    # 2. size
    ser.write(f"size {total:X}\r\n".encode())
    resp = read_until(ser, "data <hex>", 10)
    if "data <hex>" not in resp:
        print(f"  [FAIL] bad size response: {resp.strip()[:80]}")
        return False

    # 3. send the blocks
    sent = 0
    for idx in range(total_blocks):
        off = idx * BLOCK
        chunk = image[off:off + BLOCK]
        last = (idx == total_blocks - 1)

        if last:
            # last block: the firmware replies rebooting (no ACK)
            ser.write(f"data {off:X} ".encode() + chunk.hex().encode() + b"\r\n")
            resp = read_until(ser, "rebooting", 40)
            if "rebooting" in resp:
                sent += len(chunk)
                if verbose:
                    print(f"  sent {sent}/{total}")
                    print(f"  [DONE] {resp.strip()[:80]}")
                return True
            print(f"  [FAIL] no rebooting response after the last block: {resp.strip()[:80]}")
            return False

        ok = False
        for attempt in range(50):
            ser.write(f"data {off:X} ".encode() + chunk.hex().encode() + b"\r\n")
            resp = read_until_ack(ser, 5)
            if f"ACK {off + len(chunk):X}" in resp:
                sent += len(chunk)
                ok = True
                break
            ser.reset_input_buffer()  # bad line / timeout -> resend the block
        if not ok:
            print(f"  [FAIL] block 0x{off:X} failed repeatedly, giving up")
            return False
        if verbose and (sent % 16384 == 0 or sent >= total):
            print(f"  sent {sent}/{total}")

    return False  # unreachable (the last block returns)


# --------------------------------------------------------------------------
# Wait for the firmware to come up after the reboot
# --------------------------------------------------------------------------
def wait_app_ready(ser, timeout=15):
    """After reset, wait for 'PSU CMD: ready'."""
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        if ser.in_waiting:
            data = ser.read(ser.in_waiting)
            buf += data
            if b"PSU CMD: ready" in buf:
                return True, buf.decode(errors="replace")
        time.sleep(0.05)
    return False, buf.decode(errors="replace")


# --------------------------------------------------------------------------
# Main: single run or N rounds
# --------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser(description="CiosZhong PSU serial firmware upgrade")
    ap.add_argument("port", help="serial port, e.g. /dev/cu.usbserial-110")
    ap.add_argument("firmware", help="signed firmware zephyr.signed.bin")
    ap.add_argument("--count", type=int, default=1, help="number of rounds (default 1)")
    ap.add_argument("--stop-on-fail", action="store_true", help="stop at the first failure")
    ap.add_argument("--interval", type=float, default=0.0,
                    help="extra seconds between rounds (default 0)")
    ap.add_argument("--quiet", action="store_true", help="less output")
    args = ap.parse_args()

    with open(args.firmware, "rb") as f:
        image = f.read()
    total = len(image)
    print(f"firmware: {args.firmware} ({total} bytes)")
    print(f"port: {args.port}   rounds: {args.count}")

    ok_count = 0
    fail_count = 0
    t_start = time.time()

    for i in range(1, args.count + 1):
        print(f"\n===== round {i}/{args.count} =====")
        # Reconnect every round (the old handle dies after a reboot)
        try:
            ser = serial.Serial(args.port, 115200, timeout=0.1)
            ser.reset_input_buffer()
        except serial.SerialException as e:
            print(f"  [FAIL] cannot open the port: {e}")
            fail_count += 1
            if args.stop_on_fail:
                break
            time.sleep(2)
            continue

        try:
            success = upgrade_once(ser, image, verbose=not args.quiet)
            ser.close()  # close before the reboot

            if success:
                # wait for the app after the reboot (reopen the port)
                time.sleep(1.5)  # give MCUboot and the app time to come up
                ser2 = serial.Serial(args.port, 115200, timeout=0.1)
                ready, _ = wait_app_ready(ser2, timeout=15)
                ser2.close()
                if ready:
                    ok_count += 1
                    print(f"  [OK] round {i} succeeded, app is up")
                else:
                    # upload OK but the app never confirmed: count as failure
                    print(f"  [FAIL] upload OK but the app did not come up")
                    fail_count += 1
                    if args.stop_on_fail:
                        break
            else:
                fail_count += 1
                if args.stop_on_fail:
                    break
        except serial.SerialException as e:
            print(f"  [FAIL] serial error: {e}")
            fail_count += 1
            if args.stop_on_fail:
                break

        if args.interval > 0 and i < args.count:
            print(f"  waiting {args.interval}s ...")
            time.sleep(args.interval)

    elapsed = time.time() - t_start
    print("\n================= Summary =================")
    print(f"rounds: {args.count}   ok: {ok_count}   failed: {fail_count}")
    if ok_count + fail_count > 0:
        print(f"success rate: {ok_count * 100.0 / (ok_count + fail_count):.1f}%")
    print(f"elapsed: {elapsed:.1f}s")
    sys.exit(0 if fail_count == 0 else 1)


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""
EPSU OTA end-to-end test.

Modes:
  1. Same-image OTA:    --image <firmware.bin>
  2. Swap round-trip:   --image <main.bin> --swap-image <bootloader.bin>

Swap round-trip flow:
  POST /api/v1/bootloader      → OTA Bootloader → reboot → Bootloader online
  POST /api/v1/bootloader/exit → OTA Main App   → reboot → Main App online
"""

import argparse
import http.server
import json
import os
import socket
import sys
import threading
import time
import urllib.request
import urllib.error

_fw_dir = None

def _make_handler(fw_dir):
    """Return a handler that serves files from fw_dir."""
    class _FirmwareHandler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            path = self.path.lstrip("/")
            if path == "":
                self.send_response(400)
                self.end_headers()
                return
            fpath = os.path.join(fw_dir, os.path.basename(path))
            if not os.path.isfile(fpath):
                print(f"  [FW server] 404: {path}")
                self.send_response(404)
                self.end_headers()
                return
            fsize = os.path.getsize(fpath)
            print(f"  [FW server] GET {path} — serving {fsize} bytes")
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(fsize))
            self.end_headers()
            with open(fpath, "rb") as f:
                self.wfile.write(f.read())

        def log_message(self, fmt, *args):
            pass

    return _FirmwareHandler


def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("192.0.2.1", 1))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"


def http_get_json(host, port, path, timeout=5):
    url = f"http://{host}:{port}{path}"
    try:
        with urllib.request.urlopen(url, timeout=timeout) as resp:
            return resp.status, json.loads(resp.read().decode("utf-8"))
    except Exception as e:
        return None, str(e)


def http_post_json(host, port, path, body_dict, timeout=5):
    url = f"http://{host}:{port}{path}"
    data = json.dumps(body_dict).encode("utf-8")
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, json.loads(resp.read().decode("utf-8"))
    except urllib.error.HTTPError as e:
        try:
            body = e.read().decode("utf-8").strip()
            return e.code, json.loads(body) if body else {}
        except (json.JSONDecodeError, ValueError):
            return e.code, {}
    except Exception as e:
        return None, str(e)


def wait_for_device(host, port, timeout=30):
    """Wait for device HTTP server to become reachable."""
    for i in range(timeout):
        status, _ = http_get_json(host, port, "/api/v1/status/heartbeat", timeout=2)
        if status == 200:
            return True, i + 1
        time.sleep(1)
    return False, timeout


def wait_for_offline(host, port, timeout=60):
    """Wait for device to go offline (rebooting). Requires 3 consecutive failures."""
    fails = 0
    for i in range(timeout):
        status, _ = http_get_json(host, port, "/api/v1/status/heartbeat", timeout=1)
        if status is None:
            fails += 1
            if fails >= 3:
                return True, i + 1
        else:
            fails = 0
        time.sleep(1)
    return False, timeout


STATE_NAMES = {
    0: "IDLE", 1: "REQUESTED", 2: "DOWNLOADING",
    3: "VERIFYING", 4: "APPLYING", 5: "REBOOT_PENDING", 6: "FAILED",
}


def run_ota(host, port, fw_url, timeout=120, label="OTA"):
    """Trigger OTA and wait for completion. Returns True on success."""
    print(f"\n[{label}] Triggering OTA...")
    print(f"  URI: {fw_url}")

    status, resp = http_post_json(host, port, "/update/start", {"uri": fw_url})
    if status != 200 or not resp.get("ok"):
        print(f"  ERROR triggering OTA: status={status}, resp={resp}")
        return False
    print(f"  OTA triggered successfully")

    print(f"  Waiting for OTA completion (timeout={timeout}s)...")
    start = time.time()
    last_state = None
    saw_reboot = False

    while time.time() - start < timeout:
        status, resp = http_get_json(host, port, "/update/status", timeout=3)
        if status is None:
            if not saw_reboot:
                print(f"  [{time.time() - start:.1f}s] device unreachable (rebooting...)")
                saw_reboot = True
            time.sleep(2)
            continue

        state = resp.get("state", -1)
        progress = resp.get("progress", 0)
        err = resp.get("last_error", 0)
        state_name = STATE_NAMES.get(state, f"UNKNOWN({state})")

        if state != last_state:
            print(f"  [{time.time() - start:.1f}s] state={state_name} progress={progress}% error={err}")
            last_state = state

        if saw_reboot and state == 0:
            print(f"  SUCCESS: Device rebooted into new firmware")
            return True
        if state == 5:  # REBOOT_PENDING
            print(f"  SUCCESS: Device is rebooting")
            return True
        if state == 6:  # FAILED
            print(f"  FAILED: error={err}")
            return False

        time.sleep(0.5)

    print(f"  TIMEOUT: OTA did not complete within {timeout}s")
    return False


def wait_for_ota(host, port, timeout=120, label="OTA"):
    """Poll /update/status until OTA completes (REBOOT_PENDING) or fails."""
    start = time.time()
    last_state = None
    while time.time() - start < timeout:
        status, resp = http_get_json(host, port, "/update/status", timeout=3)
        if status is None:
            time.sleep(2)
            continue
        state = resp.get("state", -1) if isinstance(resp, dict) else -1
        progress = resp.get("progress", 0) if isinstance(resp, dict) else 0
        err = resp.get("last_error", 0) if isinstance(resp, dict) else 0
        state_name = STATE_NAMES.get(state, f"UNKNOWN({state})")
        if state != last_state:
            print(f"  [{label}] state={state_name} progress={progress}% error={err}")
            last_state = state
        if state == 5:  # REBOOT_PENDING
            print(f"  [{label}] OTA complete, device rebooting...")
            return True
        if state == 6:  # FAILED
            print(f"  [{label}] OTA FAILED: error={err}")
            return False
        time.sleep(0.5)
    print(f"  [{label}] TIMEOUT")
    return False


def run_swap_roundtrip(host, port, main_url, bootloader_url, timeout=120):
    """Run round-trip swap test: Main→Bootloader→Main."""
    print("\n" + "=" * 50)
    print("  Bootloader Swap Round-Trip Test")
    print("=" * 50)

    # Step A: Main App → Bootloader App
    print(f"\n[Swap A] Main App → Bootloader App")
    status, resp = http_post_json(host, port, "/api/v1/bootloader",
                                   {"uri": bootloader_url})
    if status != 200 or not resp.get("ok"):
        print(f"  ERROR: status={status}, resp={resp}")
        return False

    if not wait_for_ota(host, port, timeout=timeout, label="Swap A"):
        return False

    ok, t = wait_for_offline(host, port, timeout=30)
    if not ok:
        print(f"  ERROR: device did not go offline within 30s after REBOOT_PENDING")
        return False
    print(f"  Device went offline after {t}s")

    ok, t = wait_for_device(host, port, timeout=30)
    if not ok:
        print(f"  ERROR: Bootloader did not come online within 30s")
        return False
    print(f"  Bootloader online after {t}s")

    status, _ = http_get_json(host, port, "/api/v1/status/heartbeat")
    if status != 200:
        print(f"  ERROR: Bootloader heartbeat failed (status={status})")
        return False
    print(f"  Bootloader heartbeat OK")

    # Step B: Bootloader App → Main App
    print(f"\n[Swap B] Bootloader App → Main App")
    status, resp = http_post_json(host, port, "/api/v1/bootloader/exit",
                                   {"uri": main_url})
    if status != 200 or not resp.get("ok"):
        print(f"  ERROR: status={status}, resp={resp}")
        return False

    if not wait_for_ota(host, port, timeout=timeout, label="Swap B"):
        return False

    ok, t = wait_for_offline(host, port, timeout=30)
    if not ok:
        print(f"  ERROR: device did not go offline within 30s after REBOOT_PENDING")
        return False
    print(f"  Device went offline after {t}s")

    ok, t = wait_for_device(host, port, timeout=30)
    if not ok:
        print(f"  ERROR: Main App did not come online within 30s")
        return False
    print(f"  Main App online after {t}s")

    status, _ = http_get_json(host, port, "/api/v1/status/heartbeat")
    if status != 200:
        print(f"  ERROR: Main App heartbeat failed (status={status})")
        return False
    print(f"  Main App heartbeat OK")

    print(f"\n  SUCCESS: Round-trip swap completed (Main→Bootloader→Main)")
    return True


def main():
    parser = argparse.ArgumentParser(description="EPSU OTA end-to-end test")
    parser.add_argument("--image", required=True, help="Main App signed firmware .bin")
    parser.add_argument("--swap-image", help="Bootloader App signed firmware .bin (enables round-trip swap test)")
    parser.add_argument("--host", default="192.0.2.1", help="Device IP (default: 192.0.2.1)")
    parser.add_argument("--port", type=int, default=80, help="Device HTTP port (default: 80)")
    parser.add_argument("--listen-port", type=int, default=8080,
                        help="Local port for firmware server (default: 8080)")
    parser.add_argument("--timeout", type=int, default=120,
                        help="OTA timeout in seconds (default: 120)")
    args = parser.parse_args()

    if not os.path.exists(args.image):
        print(f"ERROR: firmware image not found: {args.image}")
        return 1

    if args.swap_image and not os.path.exists(args.swap_image):
        print(f"ERROR: swap image not found: {args.swap_image}")
        return 1

    fw_dir = os.path.dirname(args.image)
    local_ip = get_local_ip()
    main_name = os.path.basename(args.image)
    main_url = f"http://{local_ip}:{args.listen_port}/{main_name}"

    swap_name = None
    bootloader_url = None
    if args.swap_image:
        swap_name = os.path.basename(args.swap_image)
        bootloader_url = f"http://{local_ip}:{args.listen_port}/{swap_name}"

    print(f"EPSU OTA Test")
    print(f"  Device:      {args.host}:{args.port}")
    print(f"  Main FW:     {args.image} ({os.path.getsize(args.image)} bytes)")
    if args.swap_image:
        print(f"  Bootloader:  {args.swap_image} ({os.path.getsize(args.swap_image)} bytes)")
    print(f"  Main URL:    {main_url}")
    if bootloader_url:
        print(f"  Boot URL:    {bootloader_url}")
    print("=" * 50)

    # Step 1: Check device is alive
    print("\n[Step 1] Checking device connectivity...")
    status, resp = http_get_json(args.host, args.port, "/api/v1/status/heartbeat")
    if status != 200:
        print(f"  ERROR: device unreachable (status={status})")
        return 1
    uptime_status, uptime_resp = http_get_json(args.host, args.port,
                                                "/api/v1/status/uptime")
    uptime_s = uptime_resp.get("uptime_ms", 0) / 1000 if uptime_status == 200 else 0
    print(f"  Device alive, uptime={uptime_s:.1f}s")

    # Step 2: Start firmware server (serve from fw directory for multiple files)
    print(f"\n[Step 2] Starting firmware server on port {args.listen_port}...")
    handler_cls = _make_handler(fw_dir)
    server = http.server.HTTPServer(("0.0.0.0", args.listen_port), handler_cls)
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()
    print(f"  Server listening on {local_ip}:{args.listen_port}")

    passed = True

    # Step 3: Same-image OTA
    print(f"\n[Step 3] Same-image OTA update...")
    if not run_ota(args.host, args.port, main_url, args.timeout, label="Same-image OTA"):
        passed = False
    else:
        # Wait for device to actually reboot (offline first, then online)
        print(f"\n[Step 4] Waiting for OTA reboot...")
        ok, t = wait_for_offline(args.host, args.port, timeout=30)
        if ok:
            print(f"  Device went offline after {t}s")
        ok, t = wait_for_device(args.host, args.port, timeout=30)
        if not ok:
            print(f"  ERROR: device did not come back within 30s")
            passed = False
        else:
            print(f"  Device back online after {t}s")

    # Step 5: Bootloader swap round-trip (if --swap-image provided)
    if args.swap_image and passed:
        print(f"\n[Step 5] Bootloader swap round-trip test...")
        if not run_swap_roundtrip(args.host, args.port, main_url, bootloader_url, args.timeout):
            passed = False

        # Final: verify Main App is back
        if passed:
            print(f"\n[Step 6] Final verification...")
            status, resp = http_get_json(args.host, args.port, "/api/v1/status/heartbeat")
            if status == 200:
                print(f"  Main App running — round-trip test PASSED")
            else:
                print(f"  ERROR: Main App not reachable after swap test")
                passed = False

    server.shutdown()
    server_thread.join()

    if passed:
        print("\n" + "=" * 50)
        print("  ALL TESTS PASSED")
        print("=" * 50)
        return 0
    else:
        print("\n" + "=" * 50)
        print("  TEST FAILED")
        print("=" * 50)
        return 1


if __name__ == "__main__":
    sys.exit(main())

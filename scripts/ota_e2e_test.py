#!/usr/bin/env python3
"""
EPSU OTA end-to-end test.

This script:
  1. Starts an HTTP server to serve firmware image
  2. Triggers OTA on the device
  3. Polls update status until complete or failed
  4. Reports the result

Usage:
  python3 ota_e2e_test.py --image <firmware.bin> [--host <device_ip>] [--listen-port 8080]
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

# Module-level state for the firmware handler (so HTTPServer can instantiate it)
_fw_path = None
_fw_size = 0


def _make_handler(fw_path, fw_size):
    """Return a handler class bound to the given firmware file."""
    class _FirmwareHandler(http.server.BaseHTTPRequestHandler):
        def do_GET(self):
            print(f"  [FW server] GET {self.path} — serving {fw_size} bytes")
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(fw_size))
            self.end_headers()
            with open(fw_path, "rb") as f:
                self.wfile.write(f.read())

        def log_message(self, fmt, *args):
            pass  # suppress default logging

    return _FirmwareHandler


def get_local_ip():
    """Get the local IP address of this machine for the firmware URL."""
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
        return e.code, json.loads(e.read().decode("utf-8"))
    except Exception as e:
        return None, str(e)


STATE_NAMES = {
    0: "IDLE",
    1: "REQUESTED",
    2: "DOWNLOADING",
    3: "VERIFYING",
    4: "APPLYING",
    5: "REBOOT_PENDING",
    6: "FAILED",
}


def main():
    parser = argparse.ArgumentParser(description="EPSU OTA end-to-end test")
    parser.add_argument("--image", required=True, help="Path to signed firmware .bin")
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

    fw_size = os.path.getsize(args.image)
    local_ip = get_local_ip()
    fw_url = f"http://{local_ip}:{args.listen_port}/{os.path.basename(args.image)}"

    print(f"EPSU OTA Test")
    print(f"  Device:    {args.host}:{args.port}")
    print(f"  Firmware:  {args.image} ({fw_size} bytes)")
    print(f"  FW URL:    {fw_url}")
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

    # Step 2: Start firmware server
    print(f"\n[Step 2] Starting firmware server on port {args.listen_port}...")
    handler_cls = _make_handler(args.image, fw_size)
    server = http.server.HTTPServer(("0.0.0.0", args.listen_port), handler_cls)
    server_thread = threading.Thread(target=server.serve_forever, daemon=True)
    server_thread.start()
    print(f"  Server listening on {local_ip}:{args.listen_port}")

    # Step 3: Trigger OTA
    print(f"\n[Step 3] Triggering OTA...")
    status, resp = http_post_json(args.host, args.port, "/update/start",
                                   {"uri": fw_url})
    if status != 200 or not resp.get("ok"):
        print(f"  ERROR: status={status}, resp={resp}")
        server.shutdown()
        return 1
    print(f"  OTA triggered successfully")

    # Step 4: Poll for completion
    print(f"\n[Step 4] Waiting for OTA completion (timeout={args.timeout}s)...")
    start = time.time()
    last_state = None
    saw_reboot = False

    while time.time() - start < args.timeout:
        status, resp = http_get_json(args.host, args.port, "/update/status")
        if status is None:
            # Device unreachable — likely rebooting
            if not saw_reboot:
                elapsed = time.time() - start
                print(f"  [{elapsed:.1f}s] device unreachable (rebooting...)")
                saw_reboot = True
            time.sleep(2)
            continue

        state = resp.get("state", -1)
        progress = resp.get("progress", 0)
        err = resp.get("last_error", 0)
        state_name = STATE_NAMES.get(state, f"UNKNOWN({state})")

        if state != last_state:
            elapsed = time.time() - start
            print(f"  [{elapsed:.1f}s] state={state_name} progress={progress}% error={err}")
            last_state = state

        # After reboot, device comes back with state=IDLE and empty URI
        if saw_reboot and state == 0:
            print(f"\n  SUCCESS: Device rebooted into new firmware")
            server.shutdown()
            server_thread.join()
            return 0

        if state == 5:  # REBOOT_PENDING
            print(f"\n  SUCCESS: Device is rebooting into new firmware")
            server.shutdown()
            server_thread.join()
            return 0
        elif state == 6:  # FAILED
            print(f"\n  FAILED: error={err}")
            server.shutdown()
            server_thread.join()
            return 1

        time.sleep(0.5)

    print(f"\n  TIMEOUT: OTA did not complete within {args.timeout}s")
    server.shutdown()
    server_thread.join()
    return 1


if __name__ == "__main__":
    sys.exit(main())

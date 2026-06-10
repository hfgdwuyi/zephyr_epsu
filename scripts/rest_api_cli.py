#!/usr/bin/env python3
"""Simple CLI client for EPSU REST API.

Examples:
  python scripts/rest_api_cli.py --host 192.0.2.1 heartbeat
  python scripts/rest_api_cli.py --host 192.0.2.1 uptime
  python scripts/rest_api_cli.py --host 192.0.2.1 version
  python scripts/rest_api_cli.py --host 192.0.2.1 update-status

  python scripts/rest_api_cli.py --host 192.0.2.1 control --action 1 --index 1 --on 1 --value 0
  python scripts/rest_api_cli.py --host 192.0.2.1 diag-clear --mask 1 --clear-latched

  python scripts/rest_api_cli.py --host 192.0.2.1 update-start --uri http://192.0.2.2:8080/zephyr.signed.bin
  python scripts/rest_api_cli.py --host 192.0.2.1 poll-update --count 10 --interval 2

  python scripts/rest_api_cli.py --host 192.0.2.1 bootloader-enter --uri http://192.0.2.2:8080/bootloader.signed.bin
  python scripts/rest_api_cli.py --host 192.0.2.1 bootloader-exit --uri http://192.0.2.2:8080/main.signed.bin
"""

from __future__ import annotations

import argparse
import json
import sys
import time
import urllib.error
import urllib.request
from typing import Any, Dict, Tuple


def http_get(host: str, port: int, path: str, timeout: float) -> Tuple[int | None, str]:
    url = f"http://{host}:{port}{path}"
    req = urllib.request.Request(url, method="GET")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read().decode("utf-8", errors="replace")
    except Exception as exc:  # pylint: disable=broad-except
        return None, str(exc)


def http_post(host: str, port: int, path: str, body_dict: Dict[str, Any], timeout: float) -> Tuple[int | None, str]:
    url = f"http://{host}:{port}{path}"
    data = json.dumps(body_dict).encode("utf-8")
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode("utf-8", errors="replace")
    except urllib.error.HTTPError as exc:
        return exc.code, exc.read().decode("utf-8", errors="replace")
    except Exception as exc:  # pylint: disable=broad-except
        return None, str(exc)


def print_response(status: int | None, body: str) -> int:
    print(f"status: {status}")
    body = body.strip()
    if not body:
        print("body: <empty>")
        return 0 if status and 200 <= status < 300 else 1

    try:
        obj = json.loads(body)
        print("body:")
        print(json.dumps(obj, ensure_ascii=False, indent=2))
    except json.JSONDecodeError:
        print("body:")
        print(body)

    return 0 if status and 200 <= status < 300 else 1


def add_common_args(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--host", default="192.0.2.1", help="Device host, default: 192.0.2.1")
    parser.add_argument("--port", type=int, default=80, help="Device HTTP port, default: 80")
    parser.add_argument("--timeout", type=float, default=5.0, help="HTTP timeout seconds, default: 5")


def main() -> int:
    parser = argparse.ArgumentParser(description="EPSU REST API CLI")
    add_common_args(parser)

    sub = parser.add_subparsers(dest="cmd", required=True)

    sub.add_parser("heartbeat", help="GET /api/v1/status/heartbeat")
    sub.add_parser("uptime", help="GET /api/v1/status/uptime")
    sub.add_parser("version", help="GET /api/v1/status/version")
    sub.add_parser("update-status", help="GET /api/v1/update/status")
    sub.add_parser("diag-status", help="GET /api/v1/diag/status")

    p_control = sub.add_parser("control", help="POST /api/v1/control")
    p_control.add_argument("--action", type=int, required=True, help="1=LED, 2=RELAY, 3=PSU_CH, 4=BUZZER")
    p_control.add_argument("--index", type=int, required=True, help="Target index")
    p_control.add_argument("--on", type=int, choices=[0, 1], required=True, help="0=off, 1=on")
    p_control.add_argument("--value", type=int, default=0, help="Optional action value")

    p_diag_clear = sub.add_parser("diag-clear", help="POST /api/v1/diag/clear")
    p_diag_clear.add_argument("--mask", type=int, required=True, help="Fault mask")
    p_diag_clear.add_argument("--clear-latched", action="store_true", help="Also clear latched faults")

    p_update_start = sub.add_parser("update-start", help="POST /api/v1/update/start")
    p_update_start.add_argument("--uri", required=True, help="Signed image URI")

    p_poll = sub.add_parser("poll-update", help="Repeatedly GET /api/v1/update/status")
    p_poll.add_argument("--count", type=int, default=10, help="Poll count, default: 10")
    p_poll.add_argument("--interval", type=float, default=2.0, help="Interval seconds, default: 2")

    p_bl_enter = sub.add_parser("bootloader-enter", help="POST /api/v1/bootloader")
    p_bl_enter.add_argument("--uri", required=True, help="Bootloader image URI")

    p_bl_exit = sub.add_parser("bootloader-exit", help="POST /api/v1/bootloader/exit")
    p_bl_exit.add_argument("--uri", required=True, help="Main image URI")

    args = parser.parse_args()

    if args.cmd == "heartbeat":
        return print_response(*http_get(args.host, args.port, "/api/v1/status/heartbeat", args.timeout))
    if args.cmd == "uptime":
        return print_response(*http_get(args.host, args.port, "/api/v1/status/uptime", args.timeout))
    if args.cmd == "version":
        return print_response(*http_get(args.host, args.port, "/api/v1/status/version", args.timeout))
    if args.cmd == "update-status":
        return print_response(*http_get(args.host, args.port, "/api/v1/update/status", args.timeout))
    if args.cmd == "diag-status":
        return print_response(*http_get(args.host, args.port, "/api/v1/diag/status", args.timeout))

    if args.cmd == "control":
        payload = {
            "action": args.action,
            "index": args.index,
            "on": args.on,
            "value": args.value,
        }
        return print_response(*http_post(args.host, args.port, "/api/v1/control", payload, args.timeout))

    if args.cmd == "diag-clear":
        payload = {
            "mask": args.mask,
            "clear_latched": bool(args.clear_latched),
        }
        return print_response(*http_post(args.host, args.port, "/api/v1/diag/clear", payload, args.timeout))

    if args.cmd == "update-start":
        payload = {"uri": args.uri}
        return print_response(*http_post(args.host, args.port, "/api/v1/update/start", payload, args.timeout))

    if args.cmd == "bootloader-enter":
        payload = {"uri": args.uri}
        return print_response(*http_post(args.host, args.port, "/api/v1/bootloader", payload, args.timeout))

    if args.cmd == "bootloader-exit":
        payload = {"uri": args.uri}
        return print_response(*http_post(args.host, args.port, "/api/v1/bootloader/exit", payload, args.timeout))

    if args.cmd == "poll-update":
        rc = 0
        for idx in range(1, args.count + 1):
            print(f"poll #{idx}")
            s, b = http_get(args.host, args.port, "/api/v1/update/status", args.timeout)
            rc = max(rc, print_response(s, b))
            if idx != args.count:
                time.sleep(args.interval)
        return rc

    print(f"unknown command: {args.cmd}")
    return 2


if __name__ == "__main__":
    sys.exit(main())

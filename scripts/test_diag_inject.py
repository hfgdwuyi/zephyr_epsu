#!/usr/bin/env python3
"""POST /api/v1/diag/inject — inject diagnostic events (debug endpoint).

Requires CONFIG_HTTP_DIAG_DEBUG=y in firmware.
"""
import json, sys
from test_common import http_get, http_post, ok, fail, warn

def test(host="192.0.2.1", port=80):
    passed = 0
    total = 6

    # 1. Inject event with set_active_mask (sets fault bit)
    _, body = http_post(host, "/api/v1/diag/inject", {
        "code": 4660,
        "sev": 2,       # ERROR
        "aux": 42,
        "set_active_mask": 0x01,
        "set_latched_mask": 0,
    }, port)
    try:
        obj = json.loads(body)
    except json.JSONDecodeError:
        obj = {}
    if obj.get("ok"):
        ok("inject event (code=4660, sev=ERROR, set_active=0x01) -> ok")
        passed += 1
    else:
        fail(f"inject event -> {body.strip()}")

    # 2. Verify event appears in snapshot
    _, body = http_get(host, "/api/v1/diag/status", port)
    try:
        snap = json.loads(body)
    except json.JSONDecodeError:
        snap = {}
    if snap.get("active_faults", 0) != 0:
        ok(f"active_faults={snap['active_faults']} (fault bit set)")
        passed += 1
    else:
        fail(f"active_faults still 0 after inject with set_active_mask")

    if any(e.get("code") == 4660 for e in snap.get("events", [])):
        ok("event code=4660 found in diag snapshot")
        passed += 1
    else:
        warn("event code=4660 not in latest 8 events (may have rolled off)")

    # 3. Inject with set_latched_mask
    _, body = http_post(host, "/api/v1/diag/inject", {
        "code": 5000,
        "sev": 1,       # WARNING
        "aux": 0,
        "set_active_mask": 0,
        "set_latched_mask": 0x02,
    }, port)
    try:
        obj = json.loads(body)
    except json.JSONDecodeError:
        obj = {}
    if obj.get("ok"):
        ok("inject event (code=5000, sev=WARNING, set_latched=0x02) -> ok")
        passed += 1
    else:
        fail(f"inject latched -> {body.strip()}")

    # 4. Verify latched fault
    _, body = http_get(host, "/api/v1/diag/status", port)
    try:
        snap = json.loads(body)
    except json.JSONDecodeError:
        snap = {}
    if snap.get("latched_faults", 0) != 0:
        ok(f"latched_faults={snap['latched_faults']} (latched bit set)")
        passed += 1
    else:
        fail(f"latched_faults still 0 after inject with set_latched_mask")

    # 5. Payload too large
    status, body = http_post(host, "/api/v1/diag/inject",
                              {"x": "y" * 200}, port)
    if status == 413:
        ok("payload_too_large -> 413")
        passed += 1
    else:
        fail(f"payload_too_large -> {status} {body.strip()}")

    print(f"  {passed}/{total} passed")
    return 0 if passed == total else 1

if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "192.0.2.1"
    sys.exit(test(host))

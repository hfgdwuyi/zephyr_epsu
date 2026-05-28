#!/usr/bin/env python3
"""
Diagnostic lifecycle test: inject → verify → clear → verify cleared.

Usage: python3 diag_flow.py [host] [port]
"""

import json, sys
from test_common import http_get, http_post, ok, fail, warn

def test(host="192.0.2.1", port=80):
    passed = 0
    total = 6

    # 1. Connectivity
    status, _ = http_get(host, "/api/v1/status/heartbeat", port)
    if status == 200:
        ok("device reachable")
        passed += 1
    else:
        fail("device unreachable")
        return 1

    # 2. Read initial snapshot
    _, body = http_get(host, "/api/v1/diag/status", port)
    try:
        initial = json.loads(body)
    except json.JSONDecodeError:
        fail("initial snapshot: invalid JSON")
        return 1
    ok(f"initial: active_faults={initial.get('active_faults', '?')}, "
       f"latched_faults={initial.get('latched_faults', '?')}, "
       f"events={len(initial.get('events', []))}")
    passed += 1

    # 3. Inject event + set active fault bit
    _, body = http_post(host, "/api/v1/diag/inject", {
        "code": 1000,
        "sev": 2,
        "aux": 42,
        "set_active_mask": 0x01,
        "set_latched_mask": 0x01,
    }, port)
    try:
        obj = json.loads(body)
    except json.JSONDecodeError:
        obj = {}
    if obj.get("ok"):
        ok("inject event (code=1000 sev=ERROR, active=0x01, latched=0x01)")
        passed += 1
    else:
        fail(f"inject failed: {body.strip()}")
        return 1

    # 4. Verify fault appears
    _, body = http_get(host, "/api/v1/diag/status", port)
    try:
        after = json.loads(body)
    except json.JSONDecodeError:
        after = {}
    active = after.get("active_faults", 0)
    latched = after.get("latched_faults", 0)
    event_codes = [e.get("code") for e in after.get("events", [])]

    if active != 0:
        ok(f"active_faults={active} (bit set)")
        passed += 1
    else:
        fail("active_faults=0 (expected non-zero after inject)")
    if latched != 0:
        ok(f"latched_faults={latched} (bit set)")
        passed += 1
    else:
        fail("latched_faults=0 (expected non-zero after inject)")
    if 1000 in event_codes:
        ok("event code=1000 present in snapshot")
        passed += 1
    else:
        warn("event code=1000 not in latest 8 events")

    # 5. Clear faults
    _, body = http_post(host, "/api/v1/diag/clear", {
        "mask": 0xFFFFFFFF,
        "clear_latched": True,
    }, port)
    try:
        obj = json.loads(body)
    except json.JSONDecodeError:
        obj = {}
    if obj.get("ok"):
        ok("clear faults -> ok")
        passed += 1
    else:
        fail(f"clear failed: {body.strip()}")

    # 6. Verify cleared
    _, body = http_get(host, "/api/v1/diag/status", port)
    try:
        final = json.loads(body)
    except json.JSONDecodeError:
        final = {}
    if final.get("active_faults", 1) == 0 and final.get("latched_faults", 1) == 0:
        ok("all faults cleared (active=0, latched=0)")
        passed += 1
    else:
        fail(f"faults still present: active={final.get('active_faults')}, "
             f"latched={final.get('latched_faults')}")

    print(f"\n  {passed}/{total} passed")
    return 0 if passed == total else 1

if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "192.0.2.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 80
    sys.exit(test(host, port))

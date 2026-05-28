#!/usr/bin/env python3
"""POST /api/v1/diag/clear — clear diagnostic faults."""
import json, sys
from test_common import http_post, ok, fail

def test(host="192.0.2.1", port=80):
    passed = 0
    total = 3

    # Clear all active faults
    _, body = http_post(host, "/api/v1/diag/clear",
                         {"mask": -1, "clear_latched": False}, port)
    try:
        obj = json.loads(body)
    except json.JSONDecodeError:
        obj = {}
    if obj.get("ok"):
        ok("clear active faults -> ok")
        passed += 1
    else:
        fail(f"clear active faults -> {body.strip()}")

    # Clear latched faults too
    _, body = http_post(host, "/api/v1/diag/clear",
                         {"mask": -1, "clear_latched": True}, port)
    try:
        obj = json.loads(body)
    except json.JSONDecodeError:
        obj = {}
    if obj.get("ok"):
        ok("clear all (incl. latched) -> ok")
        passed += 1
    else:
        fail(f"clear all -> {body.strip()}")

    # Bad JSON
    status, body = http_post(host, "/api/v1/diag/clear",
                              {"wrong_field": 1}, port)
    try:
        obj = json.loads(body)
    except json.JSONDecodeError:
        obj = {}
    if status == 400:
        ok("bad_json -> 400")
        passed += 1
    else:
        fail(f"bad_json -> {status} {body.strip()}")

    print(f"  {passed}/{total} passed")
    return 0 if passed == total else 1

if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "192.0.2.1"
    sys.exit(test(host))

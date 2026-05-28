#!/usr/bin/env python3
"""POST /api/v1/control — LED/relay/buzzer control + error cases."""
import json, sys
from test_common import http_post, ok, fail

def test(host="192.0.2.1", port=80):
    passed = 0
    total = 5

    # Valid LED ON
    _, body = http_post(host, "/api/v1/control",
                         {"action": 1, "index": 0, "on": 1, "value": 0}, port)
    try:
        obj = json.loads(body)
    except json.JSONDecodeError:
        obj = {}
    if obj.get("ok"):
        ok("LED0 ON  -> ok")
        passed += 1
    else:
        fail(f"LED0 ON  -> {body.strip()}")

    # Valid LED OFF
    _, body = http_post(host, "/api/v1/control",
                         {"action": 1, "index": 0, "on": 0, "value": 0}, port)
    try:
        obj = json.loads(body)
    except json.JSONDecodeError:
        obj = {}
    if obj.get("ok"):
        ok("LED0 OFF -> ok")
        passed += 1
    else:
        fail(f"LED0 OFF -> {body.strip()}")

    # Bad action
    status, body = http_post(host, "/api/v1/control",
                              {"action": 99, "index": 0, "on": 1, "value": 0}, port)
    try:
        obj = json.loads(body)
    except json.JSONDecodeError:
        obj = {}
    if status == 400 and obj.get("error") == "bad_action":
        ok("bad_action -> 400")
        passed += 1
    else:
        fail(f"bad_action -> {status} {body.strip()}")

    # Bad index
    status, body = http_post(host, "/api/v1/control",
                              {"action": 1, "index": 99, "on": 1, "value": 0}, port)
    try:
        obj = json.loads(body)
    except json.JSONDecodeError:
        obj = {}
    if status == 400 and "bad_index" in obj.get("error", ""):
        ok("bad_index  -> 400")
        passed += 1
    else:
        fail(f"bad_index  -> {status} {body.strip()}")

    # Bad JSON
    status, body = http_post(host, "/api/v1/control",
                              {"invalid": "json"}, port)
    try:
        obj = json.loads(body)
    except json.JSONDecodeError:
        obj = {}
    if status == 400:
        ok("bad_json  -> 400")
        passed += 1
    else:
        fail(f"bad_json  -> {status} {body.strip()}")

    print(f"  {passed}/{total} passed")
    return 0 if passed == total else 1

if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "192.0.2.1"
    sys.exit(test(host))

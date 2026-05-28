#!/usr/bin/env python3
"""GET /update/status — OTA update status query."""
import json, sys
from test_common import http_get, ok, fail

STATE_NAMES = {
    0: "IDLE", 1: "REQUESTED", 2: "DOWNLOADING",
    3: "VERIFYING", 4: "APPLYING", 5: "REBOOT_PENDING", 6: "FAILED",
}

def test(host="192.0.2.1", port=80):
    status, body = http_get(host, "/update/status", port)
    if status != 200:
        fail(f"update/status status={status}")
        return 1
    try:
        obj = json.loads(body)
        fields = ["state", "progress", "last_error", "uri", "last_change_ts"]
        missing = [f for f in fields if f not in obj]
        if missing:
            fail(f"missing fields: {missing}")
            return 1
        state_str = STATE_NAMES.get(obj["state"], f"UNKNOWN({obj['state']})")
        ok(f"state={state_str}, progress={obj['progress']}%, "
           f"last_error={obj['last_error']}, uri='{obj['uri']}'")
        return 0
    except json.JSONDecodeError:
        fail(f"invalid JSON: {body.strip()}")
        return 1

if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "192.0.2.1"
    sys.exit(test(host))

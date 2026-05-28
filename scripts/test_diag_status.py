#!/usr/bin/env python3
"""GET /api/v1/diag/status — diagnostic snapshot query."""
import json, sys
from test_common import http_get, ok, fail

def test(host="192.0.2.1", port=80):
    status, body = http_get(host, "/api/v1/diag/status", port)
    if status != 200:
        fail(f"diag/status status={status}")
        return 1
    try:
        obj = json.loads(body)
        fields = ["active_faults", "latched_faults", "dropped_events", "events"]
        missing = [f for f in fields if f not in obj]
        if missing:
            fail(f"missing fields: {missing}")
            return 1
        ok(f"active_faults={obj['active_faults']}, "
           f"latched_faults={obj['latched_faults']}, "
           f"dropped_events={obj['dropped_events']}, "
           f"events={len(obj['events'])}")
        for e in obj["events"]:
            print(f"       event: code={e['code']} sev={e['sev']} aux={e['aux']} ts={e['ts']}")
        return 0
    except json.JSONDecodeError:
        fail(f"invalid JSON: {body.strip()}")
        return 1

if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "192.0.2.1"
    sys.exit(test(host))

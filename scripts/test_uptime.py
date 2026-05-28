#!/usr/bin/env python3
"""GET /api/v1/status/uptime — returns {"uptime_ms": <int>}."""
import json, sys
from test_common import http_get, ok, fail

def test(host="192.0.2.1", port=80):
    status, body = http_get(host, "/api/v1/status/uptime", port)
    if status != 200:
        fail(f"uptime status={status}")
        return 1
    try:
        obj = json.loads(body)
        ms = obj.get("uptime_ms", -1)
        if isinstance(ms, int) and ms > 0:
            ok(f"uptime_ms={ms} ({ms/1000:.1f}s)")
            return 0
        fail(f"invalid uptime_ms: {body.strip()}")
        return 1
    except json.JSONDecodeError:
        fail(f"invalid JSON: {body.strip()}")
        return 1

if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "192.0.2.1"
    sys.exit(test(host))

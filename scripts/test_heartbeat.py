#!/usr/bin/env python3
"""GET /api/v1/status/heartbeat — connectivity check."""
import sys
from test_common import http_get, ok, fail

def test(host="192.0.2.1", port=80):
    status, body = http_get(host, "/api/v1/status/heartbeat", port)
    if status == 200 and body.strip() == "{}":
        ok(f"heartbeat OK")
        return 0
    fail(f"heartbeat status={status}, body={body.strip()}")
    return 1

if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "192.0.2.1"
    sys.exit(test(host))

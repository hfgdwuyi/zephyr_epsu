#!/usr/bin/env python3
"""GET /api/v1/status/version — verify firmware version endpoint."""
import json, re, sys
from test_common import http_get, ok, fail

def test(host="192.0.2.1", port=80):
    status, body = http_get(host, "/api/v1/status/version", port)
    try:
        obj = json.loads(body)
    except json.JSONDecodeError:
        fail(f"invalid JSON: {body.strip()}")
        return 1

    version = obj.get("version", "")
    # Semantic version: MAJOR.MINOR.PATCH
    if re.match(r"^\d+\.\d+\.\d+$", version):
        ok(f"version={version} (valid semver)")
        return 0
    else:
        fail(f"version={version} (invalid format, expected N.N.N)")
        return 1

if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "192.0.2.1"
    sys.exit(test(host))

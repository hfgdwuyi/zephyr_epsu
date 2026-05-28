"""
Shared helpers for EPSU REST API test scripts.

Usage:
  from test_common import http_get, http_post, ok, fail, warn, Colors
"""

import json
import sys
import urllib.request
import urllib.error


class Colors:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    RESET = "\033[0m"


def ok(msg):
    print(f"  {Colors.GREEN}PASS{Colors.RESET}  {msg}")


def fail(msg):
    print(f"  {Colors.RED}FAIL{Colors.RESET}  {msg}")


def warn(msg):
    print(f"  {Colors.YELLOW}WARN{Colors.RESET}  {msg}")


def http_get(host, path, port=80, timeout=5):
    """GET request, returns (status, body_str)."""
    url = f"http://{host}:{port}{path}"
    req = urllib.request.Request(url, method="GET")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8")
    except Exception as e:
        return None, str(e)


def http_post(host, path, body_dict, port=80, timeout=5):
    """POST JSON, returns (status, body_str)."""
    url = f"http://{host}:{port}{path}"
    data = json.dumps(body_dict).encode("utf-8")
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/json")
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            return resp.status, resp.read().decode("utf-8")
    except urllib.error.HTTPError as e:
        return e.code, e.read().decode("utf-8")
    except Exception as e:
        return None, str(e)

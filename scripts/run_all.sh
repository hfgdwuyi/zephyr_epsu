#!/usr/bin/env bash
# EPSU REST API — run all tests
# Usage: ./run_all.sh [host]
set -euo pipefail

HOST="${1:-192.0.2.1}"
PORT="${2:-80}"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

GREEN='\033[0;32m'
RED='\033[0;31m'
NC='\033[0m'

PASS=0
FAIL=0
TOTAL=0

run_test() {
    local name="$1"
    local script="$2"
    echo ""
    echo "━━━ ${name} ━━━"
    if python3 "${SCRIPT_DIR}/${script}" "$HOST" "$PORT"; then
        echo -e "  ${GREEN}PASS${NC}  ${name}"
        ((PASS++)) || true
    else
        echo -e "  ${RED}FAIL${NC}  ${name}"
        ((FAIL++)) || true
    fi
    ((TOTAL++)) || true
}

echo "============================================"
echo " EPSU REST API Test Suite"
echo " Target: ${HOST}:${PORT}"
echo " $(date)"
echo "============================================"

run_test "Heartbeat"       "test_heartbeat.py"
run_test "Version"         "test_version.py"
run_test "Uptime"          "test_uptime.py"
run_test "Control"         "test_control.py"
run_test "Diag Status"     "test_diag_status.py"
run_test "Diag Clear"      "test_diag_clear.py"
run_test "Diag Inject"     "test_diag_inject.py"
run_test "Update Status"   "test_update_status.py"

echo ""
echo "============================================"
echo -e " Results: ${GREEN}${PASS} passed${NC}, ${RED}${FAIL} failed${NC}, ${TOTAL} total"
echo "============================================"

if [[ "$FAIL" -gt 0 ]]; then
    exit 1
fi

#!/usr/bin/env bash
# Diagnostic test script for zephyr_epsu PSU controller
# Usage: ./diag_test.sh [target_ip]
# Default target IP: 192.0.2.1
set -euo pipefail

TARGET="${1:-192.0.2.1}"
BASE="http://${TARGET}"
PASS=0
FAIL=0
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Extract a JSON integer value by key (works with {"key":123} format)
json_int() {
    local key="$1" json="$2"
    echo "$json" | sed -n 's/.*"'"$key"'":\([0-9]*\).*/\1/p'
}

# Check if JSON contains "key":true
json_bool_true() {
    local key="$1" json="$2"
    echo "$json" | grep -q '"'"$key"'":true'
}

check() {
    local desc="$1" expected="$2" actual="$3"
    if [[ "$actual" == "$expected" ]]; then
        echo -e "  ${GREEN}PASS${NC} $desc"
        ((PASS++)) || true
    else
        echo -e "  ${RED}FAIL${NC} $desc"
        echo "    expected: $expected"
        echo "    got:      $actual"
        ((FAIL++)) || true
    fi
}

echo "============================================"
echo " zephyr_epsu Diagnostic Test Suite"
echo " Target: $TARGET"
echo " $(date)"
echo "============================================"
echo ""

# -------------------------------------------------------------------
# 1. Connectivity check (heartbeat)
# -------------------------------------------------------------------
echo -e "${YELLOW}[1/6] Connectivity check (heartbeat)${NC}"
HB=$(curl -sS --connect-timeout 5 "${BASE}/api/v1/status/heartbeat" || echo '{"error":"unreachable"}')
if echo "$HB" | grep -q '{}'; then
    echo -e "  ${GREEN}PASS${NC} Board reachable, heartbeat OK"
    ((PASS++)) || true
else
    echo -e "  ${RED}FAIL${NC} Board unreachable or unexpected response: $HB"
    echo ""
    echo "Check: is the board powered and Ethernet connected?"
    echo "       is the IP address correct? (default 192.0.2.1)"
    exit 1
fi
echo ""

# -------------------------------------------------------------------
# 2. Get uptime
# -------------------------------------------------------------------
echo -e "${YELLOW}[2/6] Uptime check${NC}"
UPTIME=$(curl -sS --connect-timeout 5 "${BASE}/uptime" || echo "0")
echo "  Uptime: ${UPTIME} ms"
if [[ "$UPTIME" -gt 0 ]]; then
    echo -e "  ${GREEN}PASS${NC} Uptime reported"
    ((PASS++)) || true
else
    echo -e "  ${RED}FAIL${NC} Uptime not available"
    ((FAIL++)) || true
fi
echo ""

# -------------------------------------------------------------------
# 3. Read initial diagnostic snapshot
# -------------------------------------------------------------------
echo -e "${YELLOW}[3/6] Initial diagnostic snapshot${NC}"
INITIAL=$(curl -sS --connect-timeout 5 "${BASE}/diag" || echo '{}')
echo "  Response: $INITIAL"
echo "  Initial active_faults: $(json_int active_faults "$INITIAL")"
((PASS++)) || true
echo ""

# -------------------------------------------------------------------
# 4. Inject a test fault (event code 100, severity 2 = error)
# -------------------------------------------------------------------
echo -e "${YELLOW}[4/6] Inject test fault (code=100, severity=2)${NC}"
INJECT=$(curl -sS --connect-timeout 5 -X POST "${BASE}/diag/inject" \
    -H "Content-Type: application/json" \
    -d '{"code":100,"severity":2,"aux":42,"set_active_mask":1,"set_latched_mask":0}' || echo '{"ok":false}')
echo "  Response: $INJECT"
if json_bool_true ok "$INJECT"; then
    check "Inject returns ok=true" "true" "true"
else
    check "Inject returns ok=true" "true" "false"
fi
echo ""

# -------------------------------------------------------------------
# 5. Verify fault is present in snapshot
# -------------------------------------------------------------------
echo -e "${YELLOW}[5/6] Verify fault appears in snapshot${NC}"
AFTER_INJECT=$(curl -sS --connect-timeout 5 "${BASE}/diag" || echo '{}')
echo "  Response: $AFTER_INJECT"
AFTER_FAULTS=$(json_int active_faults "$AFTER_INJECT")
AFTER_FAULTS="${AFTER_FAULTS:-0}"
if [[ "$AFTER_FAULTS" -gt 0 ]]; then
    echo -e "  ${GREEN}PASS${NC} Fault is active (active_faults=${AFTER_FAULTS})"
    ((PASS++)) || true
else
    echo -e "  ${RED}FAIL${NC} Fault not detected in snapshot"
    echo "    Note: inject adds an event but does NOT set fault bits by default."
    echo "    The diag snapshot shows events[] separately from active_faults."
    echo "    Check if the event appears in the response above."
    ((FAIL++)) || true
fi
echo ""

# -------------------------------------------------------------------
# 6. Clear faults
# -------------------------------------------------------------------
echo -e "${YELLOW}[6/6] Clear all faults${NC}"
CLEAR=$(curl -sS --connect-timeout 5 -X POST "${BASE}/diag/clear" \
    -H "Content-Type: application/json" \
    -d '{"mask":-1,"clear_latched":true}' || echo '{"ok":false}')
echo "  Response: $CLEAR"
if json_bool_true ok "$CLEAR"; then
    check "Clear returns ok=true" "true" "true"
else
    check "Clear returns ok=true" "true" "false"
fi
echo ""

# -------------------------------------------------------------------
# Final: Verify faults cleared
# -------------------------------------------------------------------
echo -e "${YELLOW}[Final] Verify faults cleared${NC}"
FINAL=$(curl -sS --connect-timeout 5 "${BASE}/diag" || echo '{}')
echo "  Response: $FINAL"
FINAL_FAULTS=$(json_int active_faults "$FINAL")
FINAL_FAULTS="${FINAL_FAULTS:-0}"
if [[ "$FINAL_FAULTS" -eq 0 ]]; then
    echo -e "  ${GREEN}PASS${NC} All faults cleared (active_faults=0)"
    ((PASS++)) || true
else
    echo -e "  ${RED}FAIL${NC} Faults still present (active_faults=${FINAL_FAULTS})"
    ((FAIL++)) || true
fi
echo ""

# -------------------------------------------------------------------
# Summary
# -------------------------------------------------------------------
echo "============================================"
echo " Results: ${GREEN}${PASS} passed${NC}, ${RED}${FAIL} failed${NC}"
echo "============================================"
if [[ "$FAIL" -gt 0 ]]; then
    exit 1
fi

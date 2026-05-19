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
INITIAL_FAULTS=$(echo "$INITIAL" | grep -o '"faults_active":[0-9]*' | grep -o '[0-9]*' || echo "0")
echo "  Initial faults_active: $INITIAL_FAULTS"
((PASS++)) || true
echo ""

# -------------------------------------------------------------------
# 4. Inject a test fault (event code 100, severity 2 = error)
# -------------------------------------------------------------------
echo -e "${YELLOW}[4/6] Inject test fault (code=100, severity=2)${NC}"
INJECT=$(curl -sS --connect-timeout 5 -X POST "${BASE}/diag/inject" \
    -H "Content-Type: application/json" \
    -d '{"code":100,"severity":2,"aux":42}' || echo '{"ok":false}')
echo "  Response: $INJECT"
INJECT_OK=$(echo "$INJECT" | grep -o '"ok":[a-z]*' | grep -o '[a-z]*' || echo "false")
check "Inject returns ok=true" "true" "$INJECT_OK"
echo ""

# -------------------------------------------------------------------
# 5. Verify fault is present in snapshot
# -------------------------------------------------------------------
echo -e "${YELLOW}[5/6] Verify fault appears in snapshot${NC}"
AFTER_INJECT=$(curl -sS --connect-timeout 5 "${BASE}/diag" || echo '{}')
echo "  Response: $AFTER_INJECT"
AFTER_FAULTS=$(echo "$AFTER_INJECT" | grep -o '"faults_active":[0-9]*' | grep -o '[0-9]*' || echo "0")
if [[ "$AFTER_FAULTS" -gt 0 ]]; then
    echo -e "  ${GREEN}PASS${NC} Fault is active (faults_active=${AFTER_FAULTS})"
    ((PASS++)) || true
else
    echo -e "  ${RED}FAIL${NC} Fault not detected in snapshot"
    ((FAIL++)) || true
fi
echo ""

# -------------------------------------------------------------------
# 6. Clear faults
# -------------------------------------------------------------------
echo -e "${YELLOW}[6/6] Clear all faults${NC}"
CLEAR=$(curl -sS --connect-timeout 5 -X POST "${BASE}/diag/clear" \
    -H "Content-Type: application/json" \
    -d '{"mask":4294967295,"clear_latched":true}' || echo '{"ok":false}')
echo "  Response: $CLEAR"
CLEAR_OK=$(echo "$CLEAR" | grep -o '"ok":[a-z]*' | grep -o '[a-z]*' || echo "false")
check "Clear returns ok=true" "true" "$CLEAR_OK"
echo ""

# -------------------------------------------------------------------
# Final: Verify faults cleared
# -------------------------------------------------------------------
echo -e "${YELLOW}[Final] Verify faults cleared${NC}"
FINAL=$(curl -sS --connect-timeout 5 "${BASE}/diag" || echo '{}')
echo "  Response: $FINAL"
FINAL_FAULTS=$(echo "$FINAL" | grep -o '"faults_active":[0-9]*' | grep -o '[0-9]*' || echo "0")
if [[ "$FINAL_FAULTS" -eq 0 ]]; then
    echo -e "  ${GREEN}PASS${NC} All faults cleared (faults_active=0)"
    ((PASS++)) || true
else
    echo -e "  ${RED}FAIL${NC} Faults still present (faults_active=${FINAL_FAULTS})"
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

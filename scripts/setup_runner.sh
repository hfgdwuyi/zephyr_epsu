#!/usr/bin/env bash
# Setup GitHub Actions self-hosted runner for zephyr_epsu HIL testing.
#
# Prerequisites:
#   - GitHub Personal Access Token with 'repo' scope (or use gh CLI)
#   - macOS with Zephyr SDK installed
#
# Usage: ./setup_runner.sh <github_token>
#
# The runner will be installed at ~/actions-runner with label 'nucleo-h745zi'.

set -euo pipefail

TOKEN="${1:-}"
if [ -z "$TOKEN" ]; then
    echo "Usage: $0 <github_pat_token>"
    echo ""
    echo "Get a token at: https://github.com/settings/tokens"
    echo "Required scopes: repo (or workflow for public repos)"
    exit 1
fi

REPO="hfgdwuyi/zephyr_epsu"
RUNNER_DIR="$HOME/actions-runner"
RUNNER_VERSION="2.323.0"
RUNNER_PKG="actions-runner-osx-arm64-${RUNNER_VERSION}.tar.gz"
RUNNER_URL="https://github.com/actions/runner/releases/download/v${RUNNER_VERSION}/${RUNNER_PKG}"

echo "=== GitHub Actions Runner Setup ==="
echo "  Repo:   $REPO"
echo "  Dir:    $RUNNER_DIR"
echo "  Labels: nucleo-h745zi"
echo ""

# Stop existing runner if running
if [ -f "$RUNNER_DIR/svc.sh" ]; then
    echo "[1/5] Stopping existing runner..."
    cd "$RUNNER_DIR"
    ./svc.sh stop 2>/dev/null || true
    ./config.sh remove --token "$TOKEN" 2>/dev/null || true
fi

# Download runner package
echo "[2/5] Downloading runner v${RUNNER_VERSION}..."
mkdir -p "$RUNNER_DIR"
cd "$RUNNER_DIR"
curl -sS -L -o "$RUNNER_PKG" "$RUNNER_URL"
tar xzf "$RUNNER_PKG"
rm -f "$RUNNER_PKG"

# Configure runner
echo "[3/5] Configuring runner..."
./config.sh \
    --url "https://github.com/$REPO" \
    --token "$TOKEN" \
    --name "mac-nucleo-h745zi" \
    --labels "nucleo-h745zi" \
    --work "$RUNNER_DIR/_work" \
    --unattended \
    --replace

# Install as launchd service
echo "[4/5] Installing as launchd service..."
./svc.sh install
./svc.sh start

echo "[5/5] Done!"
echo ""
echo "Runner status:"
./svc.sh status
echo ""
echo "Verify at: https://github.com/$REPO/settings/actions/runners"

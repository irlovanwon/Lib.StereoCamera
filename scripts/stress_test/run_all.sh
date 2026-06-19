#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DURATION="${1:-60}"
WSS_CLIENTS="${2:-5}"
ZMQ_CLIENTS="${3:-3}"

echo "============================================"
echo " StereoCamera Stress Test Suite"
echo "============================================"
echo " Duration: ${DURATION}s"
echo " WSS Clients: ${WSS_CLIENTS}"
echo " ZMQ Clients: ${ZMQ_CLIENTS}"
echo "============================================"

pip3 install -q -r "${SCRIPT_DIR}/requirements.txt" 2>/dev/null || true

echo ""
echo "=== 1/3: WSS Multi-Client Stress Test (API 3b-2) ==="
python3 "${SCRIPT_DIR}/stress_wss.py" --test all --clients "${WSS_CLIENTS}" --duration "${DURATION}" || true

echo ""
echo "=== 2/3: ZMQ Multi-Client Stress Test (API 2b) ==="
python3 "${SCRIPT_DIR}/stress_zmq.py" --test all --clients "${ZMQ_CLIENTS}" --duration "${DURATION}" || true

echo ""
echo "============================================"
echo " Stress test complete."
echo " Reports: /tmp/stress_test_reports/"
echo "============================================"

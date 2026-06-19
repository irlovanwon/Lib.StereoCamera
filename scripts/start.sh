#!/bin/bash
#
# StereoCamera - Service Management Script
# Usage: ./scripts/start.sh {start|stop|restart|status|build}
#

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
APP_NAME="stereo_camera_node"
BUILD_DIR="${PROJECT_DIR}/build"
BIN="${BUILD_DIR}/${APP_NAME}"
CONFIG_DIR="${PROJECT_DIR}/config"
PID_FILE="${PROJECT_DIR}/${APP_NAME}.pid"
LOG_DIR="${PROJECT_DIR}/log"
LOG_FILE="${LOG_DIR}/${APP_NAME}.log"

mkdir -p "${LOG_DIR}"

is_running() {
    if [ -f "${PID_FILE}" ]; then
        local pid
        pid=$(cat "${PID_FILE}")
        if kill -0 "${pid}" 2>/dev/null; then
            return 0
        fi
    fi
    return 1
}

do_build() {
    echo "[StereoCamera] Building..."
    mkdir -p "${BUILD_DIR}"
    cd "${BUILD_DIR}"
    cmake ..
    make -j$(nproc)
    echo "[StereoCamera] Build complete."
}

do_start() {
    if is_running; then
        echo "[StereoCamera] Already running (PID: $(cat "${PID_FILE}"))"
        return 0
    fi

    if [ ! -f "${BIN}" ]; then
        do_build
    fi

    echo "[StereoCamera] Starting..."
    echo "  Config: ${CONFIG_DIR}"
    echo "  Log:    ${LOG_FILE}"

    cd "${PROJECT_DIR}"
    nohup "${BIN}" "${CONFIG_DIR}" > "${LOG_FILE}" 2>&1 &
    echo $! > "${PID_FILE}"
    sleep 1

    if is_running; then
        echo "[StereoCamera] Started (PID: $(cat "${PID_FILE}"))"
    else
        echo "[StereoCamera] Failed to start — check ${LOG_FILE}"
        rm -f "${PID_FILE}"
        return 1
    fi
}

do_stop() {
    if ! is_running; then
        echo "[StereoCamera] Not running."
        rm -f "${PID_FILE}"
        return 0
    fi

    local pid
    pid=$(cat "${PID_FILE}")
    echo "[StereoCamera] Stopping (PID: ${pid})..."
    kill -TERM "${pid}" 2>/dev/null || true

    local count=0
    while kill -0 "${pid}" 2>/dev/null; do
        sleep 0.5
        count=$((count + 1))
        if [ ${count} -ge 20 ]; then
            echo "[StereoCamera] Force killing..."
            kill -9 "${pid}" 2>/dev/null || true
            break
        fi
    done

    rm -f "${PID_FILE}"
    echo "[StereoCamera] Stopped."
}

do_status() {
    if is_running; then
        echo "[StereoCamera] Running (PID: $(cat "${PID_FILE}"))"
    else
        echo "[StereoCamera] Not running."
    fi
}

case "${1:-start}" in
    start)   do_start ;;
    stop)    do_stop ;;
    restart) do_stop; sleep 1; do_start ;;
    status)  do_status ;;
    build)   do_build ;;
    *)
        echo "Usage: $0 {start|stop|restart|status|build}"
        exit 1
        ;;
esac

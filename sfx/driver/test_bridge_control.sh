#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "$0")" && pwd)
PORT=19090
UDP_LOG=$(mktemp)
BRIDGE_LOG=$(mktemp)
cleanup() {
    if [[ -n "${BRIDGE_PID:-}" ]]; then
        kill "$BRIDGE_PID" 2>/dev/null || true
        wait "$BRIDGE_PID" 2>/dev/null || true
    fi
    if [[ -n "${UDP_PID:-}" ]]; then
        kill "$UDP_PID" 2>/dev/null || true
        wait "$UDP_PID" 2>/dev/null || true
    fi
    rm -f "$UDP_LOG" "$BRIDGE_LOG"
}
trap cleanup EXIT

make -C "$SCRIPT_DIR" bridge >/dev/null
python3 -u -c 'import socket,sys; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.bind(("127.0.0.1", int(sys.argv[1]))); data,_=s.recvfrom(2048); print("[RECV %d bytes]" % len(data)); print(" ".join(f"{b:02X}" for b in data));' "$PORT" >"$UDP_LOG" 2>&1 &
UDP_PID=$!
sleep 1

"$SCRIPT_DIR"/bridge --udp 127.0.0.1:$PORT --listen-port $((PORT + 1)) --no-bridge --control 02 >"$BRIDGE_LOG" 2>&1 &
BRIDGE_PID=$!

for _ in $(seq 1 50); do
    if grep -q "\[RECV 7 bytes" "$UDP_LOG"; then
        break
    fi
    sleep 0.1
done

kill "$BRIDGE_PID" 2>/dev/null || true
wait "$BRIDGE_PID" 2>/dev/null || true

if ! grep -q "\[RECV 7 bytes" "$UDP_LOG"; then
    echo "did not receive startup control frame"
    echo "--- udp log ---"
    cat "$UDP_LOG"
    echo "--- bridge log ---"
    cat "$BRIDGE_LOG"
    exit 1
fi

if ! grep -q "01 08 13 C8 00 80 02" "$UDP_LOG"; then
    echo "unexpected control frame bytes"
    echo "--- udp log ---"
    cat "$UDP_LOG"
    echo "--- bridge log ---"
    cat "$BRIDGE_LOG"
    exit 1
fi

echo "bridge control startup injection test passed"

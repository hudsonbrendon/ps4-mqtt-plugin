#!/usr/bin/env bash
# Spins up a temporary mosquitto broker on :1883 with auth, runs the
# integration test binary, then tears the broker down.
set -euo pipefail

if ! command -v mosquitto >/dev/null; then
    echo "mosquitto not installed (brew install mosquitto / apt install mosquitto)" >&2
    exit 1
fi

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

PWFILE="$WORKDIR/passwd"
CONF="$WORKDIR/mosquitto.conf"
LOGF="$WORKDIR/mosquitto.log"

mosquitto_passwd -c -b "$PWFILE" test_user test_pass >/dev/null 2>&1

cat > "$CONF" <<EOF
listener 1883 127.0.0.1
allow_anonymous false
password_file $PWFILE
log_dest file $LOGF
EOF

mosquitto -c "$CONF" -d
BROKER_PID="$(pgrep -f "mosquitto -c $CONF" | head -1)"
trap 'kill "$BROKER_PID" 2>/dev/null || true; rm -rf "$WORKDIR"' EXIT

# Wait for broker to bind.
for _ in $(seq 1 20); do
    if (echo > /dev/tcp/127.0.0.1/1883) 2>/dev/null; then break; fi
    sleep 0.1
done

"./build/integration"

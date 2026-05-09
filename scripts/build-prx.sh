#!/usr/bin/env bash
# Build the PS4 PRX plugin using the OpenOrbis toolchain inside Docker.
# Usage: scripts/build-prx.sh
set -euo pipefail

cd "$(dirname "$0")/.."

IMAGE=ps4-mqtt-build:latest

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo ">>> Building Docker image $IMAGE (one-time, ~5 min)..."
    docker build -t "$IMAGE" -f docker/Dockerfile docker/
fi

echo ">>> Building PRX..."
docker run --rm \
    -v "$(pwd):/work" \
    -w /work \
    "$IMAGE" \
    make prx

echo ">>> Done. Output:"
ls -lh build/ps4-mqtt.prx 2>/dev/null || echo "(prx file not produced — check make output)"

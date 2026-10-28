#!/usr/bin/env bash
# Builds build-switch/OpenAoK.nro with devkitPro's Switch toolchain: the
# installed one when DEVKITPRO is set, otherwise devkitPro's Docker image.
#
#   switch/build.sh           build
#   switch/build.sh --clean   wipe build-switch/ first
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="devkitpro/devkita64:20260219"

if [ "${1:-}" = "--clean" ]; then
    rm -rf "$ROOT_DIR/build-switch"
fi

# Run from the repository's root, inside the toolchain's environment.
STEPS='
GENERATOR=""
if command -v ninja >/dev/null 2>&1; then GENERATOR="-G Ninja"; fi
cmake -S . -B build-switch $GENERATOR \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake"
JOBS="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
cmake --build build-switch --parallel "$JOBS"
'

if [ -n "${DEVKITPRO:-}" ]; then
    (cd "$ROOT_DIR" && bash -c "$STEPS")
elif command -v docker >/dev/null 2>&1; then
    docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
        -v "$ROOT_DIR":/src -w /src "$IMAGE" bash -lc "$STEPS"
else
    echo "error: neither devkitPro (DEVKITPRO) nor Docker found; see switch/README.md." >&2
    exit 1
fi

echo "==> Built $ROOT_DIR/build-switch/OpenAoK.nro"

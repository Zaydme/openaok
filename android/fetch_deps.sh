#!/usr/bin/env bash
# Fetches the libraries the Android build compiles from source -- SDL 2.32.10
# and FreeType 2.13.3 -- into android/deps (not kept in git).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p deps
cd deps
if [ ! -d SDL2-2.32.10 ]; then
    curl -fsSL -o SDL2-2.32.10.tar.gz \
        https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
    tar xzf SDL2-2.32.10.tar.gz
fi
if [ ! -d freetype-VER-2-13-3 ]; then
    curl -fsSL -o freetype-2.13.3.tar.gz \
        https://github.com/freetype/freetype/archive/refs/tags/VER-2-13-3.tar.gz
    tar xzf freetype-2.13.3.tar.gz
fi
echo "deps ready in $(pwd)"

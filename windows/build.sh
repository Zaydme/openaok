#!/usr/bin/env bash
# Builds OpenAoK.exe for 64-bit Windows on Debian or Ubuntu with MinGW-w64
# (sudo apt install mingw-w64 cmake ninja-build curl xz-utils), and lays out
# the folder to unzip on Windows in windows/build/OpenAoK: the game, SDL2.dll,
# the data tools, a folder for the CD's files, and OpenAoK.bat to start it.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(dirname "$HERE")"
WORK="$HERE/build"
export WINDOWS_PREFIX="$WORK/prefix"

SDL_VERSION=2.32.10
SDL_SHA256=83a5d74012311edc3c0d40ea6faecbe57ad692aa033fa5dc273cc937e3938ff2
FREETYPE_VERSION=2.13.3
FREETYPE_SHA256=0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289

mkdir -p "$WORK/deps" "$WINDOWS_PREFIX"
cd "$WORK/deps"
fetch() {   # url, file, sha256
    [ -f "$2" ] || curl -sSfL -o "$2" "$1"
    echo "$3  $2" | sha256sum -c --quiet -
}

# SDL2's MinGW development files, and SDL2.dll.
fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL_VERSION/SDL2-devel-$SDL_VERSION-mingw.tar.gz" \
      sdl.tar.gz "$SDL_SHA256"
tar -xf sdl.tar.gz
cp -r "SDL2-$SDL_VERSION/x86_64-w64-mingw32/." "$WINDOWS_PREFIX/"

# FreeType, a static library with nothing optional.
if [ ! -f "$WINDOWS_PREFIX/lib/libfreetype.a" ]; then
    fetch "https://download.savannah.gnu.org/releases/freetype/freetype-$FREETYPE_VERSION.tar.xz" \
          freetype.tar.xz "$FREETYPE_SHA256"
    tar -xf freetype.tar.xz
    cmake -S "freetype-$FREETYPE_VERSION" -B freetype-build -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$HERE/mingw-w64.cmake" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX="$WINDOWS_PREFIX" -DBUILD_SHARED_LIBS=OFF \
        -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON \
        -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
    cmake --build freetype-build
    cmake --install freetype-build
fi

# The dat dumper the data tools run (tools/data/datfile.py), from a copy of
# genieutils with the win-iconv it expects in extern/.
WIN_ICONV_VERSION=0.0.10
WIN_ICONV_SHA256=58493387c7c9c70d61e711ec2feec5db0a59d164556642d2b427dde4ef756bc1
fetch "https://github.com/win-iconv/win-iconv/archive/refs/tags/v$WIN_ICONV_VERSION.tar.gz" \
      win-iconv.tar.gz "$WIN_ICONV_SHA256"
rm -rf genieutils "win-iconv-$WIN_ICONV_VERSION"
tar -xf win-iconv.tar.gz
cp -r "$REPO/third_party/genieutils" genieutils
mkdir -p genieutils/extern
cp -r "win-iconv-$WIN_ICONV_VERSION" genieutils/extern/win-iconv
cmake -S "$REPO/tools/data" -B "$WORK/data-tools" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$HERE/mingw-w64.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DGENIEUTILS_DIR="$WORK/deps/genieutils"
cmake --build "$WORK/data-tools" --target dump_dat

# The game.
cmake -S "$REPO" -B "$WORK/game" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$HERE/mingw-w64.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$WINDOWS_PREFIX"
cmake --build "$WORK/game"

# The folder to unzip: laid out as the source tree is, so the game, run from
# build\, finds ..\assets and ..\original_game_1999.
OUT="$WORK/OpenAoK"
rm -rf "$OUT"
mkdir -p "$OUT/build/data-tools" "$OUT/tools/data" "$OUT/original_game_1999"
cp "$WORK/game/OpenAoK.exe" "$WINDOWS_PREFIX/bin/SDL2.dll" "$OUT/build/"
cp "$WORK/data-tools/dump_dat.exe" "$OUT/build/data-tools/"
cp "$REPO"/tools/data/*.py "$OUT/tools/data/"
cp "$HERE/OpenAoK.bat" "$OUT/"
cp "$HERE/README.md" "$OUT/README.md"
cp "$REPO/LICENSE" "$REPO/THIRD_PARTY_NOTICES.md" "$OUT/"
cp -r "$REPO/LICENSES" "$OUT/"
echo "Put the CD's files here." > "$OUT/original_game_1999/PUT THE CD'S FILES HERE.txt"
echo "$OUT"

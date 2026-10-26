#!/usr/bin/env bash
# Configure and build OpenAoK on macOS or Linux.
#
#   ./build.sh                 configure + build (Release)
#   ./build.sh --debug         configure + build (Debug)
#   ./build.sh --clean         wipe build/ first
#   ./build.sh --run           build, then launch the game
#
# The game loads assets via paths relative to the build directory
# (e.g. "../assets/..."), so it must be run from inside build/.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"
BUILD_TYPE="Release"
CLEAN=0
RUN=0

for arg in "$@"; do
    case "$arg" in
        --debug)   BUILD_TYPE="Debug" ;;
        --release) BUILD_TYPE="Release" ;;
        --clean)   CLEAN=1 ;;
        --run)     RUN=1 ;;
        -h|--help) sed -n '2,10p' "${BASH_SOURCE[0]}" | sed 's/^# \?//'; exit 0 ;;
        *) echo "error: unknown option '$arg' (try --help)" >&2; exit 2 ;;
    esac
done

OS="$(uname -s)"

# --- dependency checks -------------------------------------------------------

if ! command -v cmake >/dev/null 2>&1; then
    if [ "$OS" = "Darwin" ]; then
        echo "error: cmake not found. Install it with: brew install cmake" >&2
    else
        echo "error: cmake not found. Install it with: sudo apt install cmake" >&2
    fi
    exit 1
fi

if [ "$OS" = "Darwin" ]; then
    # SDL2 is not part of the macOS SDK; Homebrew is how it gets here.
    if ! command -v brew >/dev/null 2>&1; then
        echo "error: Homebrew not found. See https://brew.sh -- SDL2 is needed." >&2
        exit 1
    fi
    if [ ! -d "$(brew --prefix)/lib/cmake/SDL2" ]; then
        echo "error: SDL2 not installed. Install it with: brew install sdl2" >&2
        exit 1
    fi
    if [ ! -d "$(brew --prefix)/include/freetype2" ]; then
        echo "error: FreeType not installed. Install it with: brew install freetype" >&2
        exit 1
    fi
    JOBS="$(sysctl -n hw.ncpu)"
else
    JOBS="$(nproc)"
fi

# --- game data ---------------------------------------------------------------

if ! command -v python3 >/dev/null 2>&1; then
    echo "error: python3 not found; it prepares the game data from original_game_1999/." >&2
    exit 1
fi
python3 "$ROOT_DIR/tools/data/prepare_data.py"

# --- stale cache handling ----------------------------------------------------

# A CMakeCache.txt copied from another machine (or another checkout path) pins
# absolute paths to the old compiler and source tree; CMake refuses to reuse it.
if [ "$CLEAN" -eq 0 ] && [ -f "$BUILD_DIR/CMakeCache.txt" ]; then
    CACHED_SRC="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$BUILD_DIR/CMakeCache.txt")"
    if [ "$CACHED_SRC" != "$ROOT_DIR" ]; then
        echo "==> Stale CMake cache (configured for '$CACHED_SRC'); wiping build/"
        CLEAN=1
    fi
fi

if [ "$CLEAN" -eq 1 ]; then
    rm -rf "$BUILD_DIR"
fi

# --- configure + build -------------------------------------------------------

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
    GENERATOR_ARGS=(-G Ninja)
fi

echo "==> Configuring ($OS, $BUILD_TYPE, $JOBS jobs)"
# macOS ships Bash 3.2, where "${arr[@]}" on an empty array trips `set -u`;
# the ${arr[@]+...} guard keeps this working on both Bash 3 and Bash 4+.
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    ${GENERATOR_ARGS[@]+"${GENERATOR_ARGS[@]}"}

echo "==> Building"
cmake --build "$BUILD_DIR" --parallel "$JOBS"

echo "==> Built $BUILD_DIR/OpenAoK"

if [ "$RUN" -eq 1 ]; then
    echo "==> Running"
    cd "$BUILD_DIR" && exec ./OpenAoK
fi

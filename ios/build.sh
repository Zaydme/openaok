#!/usr/bin/env bash
# Writes OpenAoK.xcodeproj (ios/build-sim/ for the simulator, ios/build/ for a
# device) and builds OpenAoK.app with it.
#
#   ios/build.sh               build for the simulator (no signing needed)
#   ios/build.sh --device      build for a device; unsigned unless
#                              OPENAOK_TEAM_ID names an Apple team
#   ios/build.sh --device --ipa  and pack it as ios/build/OpenAoK.ipa
#   ios/build.sh --project     only write the Xcode project
#
# OPENAOK_JOBS sets how many files compile at once (4 by default);
# OPENAOK_VERSION the app's version ("v0.6.0"), 0.0.0 when unset;
# OPENAOK_CCACHE=1 compiles through ccache (brew install ccache).
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TARGET="simulator"
PROJECT_ONLY=0
IPA=0

for arg in "$@"; do
    case "$arg" in
        --simulator) TARGET="simulator" ;;
        --device)    TARGET="device" ;;
        --project)   PROJECT_ONLY=1 ;;
        --ipa)       IPA=1 ;;
        -h|--help)   sed -n '2,13p' "${BASH_SOURCE[0]}" | sed -E 's/^# ?//'; exit 0 ;;
        *) echo "error: unknown option '$arg' (try --help)" >&2; exit 2 ;;
    esac
done

if [ "$IPA" -eq 1 ] && [ "$TARGET" != "device" ]; then
    echo "error: --ipa packs a device build: add --device" >&2
    exit 2
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "error: cmake not found. Install it with: brew install cmake" >&2
    exit 1
fi
if ! xcode-select -p >/dev/null 2>&1; then
    echo "error: Xcode not found. Install it from the App Store and open it once." >&2
    exit 1
fi

if [ ! -d "$ROOT_DIR/ios/deps/SDL2-2.32.10" ] || [ ! -d "$ROOT_DIR/ios/deps/freetype-VER-2-13-3" ]; then
    "$ROOT_DIR/ios/fetch_deps.sh"
fi

if [ "$TARGET" = "device" ]; then
    BUILD_DIR="$ROOT_DIR/ios/build"
    SYSROOT="iphoneos"
else
    BUILD_DIR="$ROOT_DIR/ios/build-sim"
    SYSROOT="iphonesimulator"
fi

JOBS="${OPENAOK_JOBS:-4}"
OPTIONS=()
if [ -n "${OPENAOK_TEAM_ID:-}" ]; then
    OPTIONS+=(-DOPENAOK_TEAM_ID="$OPENAOK_TEAM_ID")
fi
if [ -n "${OPENAOK_CCACHE:-}" ]; then
    OPTIONS+=(-DOPENAOK_CCACHE=ON)
fi

echo "==> Writing $BUILD_DIR/OpenAoK.xcodeproj"
# macOS ships Bash 3.2, where "${arr[@]}" on an empty array trips `set -u`;
# the ${arr[@]+...} guard keeps this working on both Bash 3 and Bash 4+.
cmake -G Xcode -S "$ROOT_DIR/ios" -B "$BUILD_DIR" \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT="$SYSROOT" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    ${OPTIONS[@]+"${OPTIONS[@]}"}

if [ "$PROJECT_ONLY" -eq 1 ]; then
    echo "==> Open $BUILD_DIR/OpenAoK.xcodeproj"
    exit 0
fi

echo "==> Building ($TARGET, $JOBS jobs)"
# A device build without a team is left unsigned: AltStore signs the .ipa
# itself, and Xcode signs it once a team is chosen in the project.
if [ "$TARGET" = "device" ] && [ -z "${OPENAOK_TEAM_ID:-}" ]; then
    cmake --build "$BUILD_DIR" --config Release --parallel "$JOBS" \
        -- CODE_SIGNING_ALLOWED=NO
else
    cmake --build "$BUILD_DIR" --config Release --parallel "$JOBS"
fi

APP="$BUILD_DIR/Release-$SYSROOT/OpenAoK.app"
echo "==> Built $APP"

# An .ipa is the app in a Payload folder, zipped: what AltStore installs and
# signs.
if [ "$IPA" -eq 1 ]; then
    STAGE="$(mktemp -d)"
    mkdir "$STAGE/Payload"
    cp -R "$APP" "$STAGE/Payload/"
    rm -f "$BUILD_DIR/OpenAoK.ipa"
    (cd "$STAGE" && zip -qry "$BUILD_DIR/OpenAoK.ipa" Payload)
    rm -rf "$STAGE"
    echo "==> Packed $BUILD_DIR/OpenAoK.ipa"
fi

#!/usr/bin/env bash
# Adds FILE to the release of the version tag being built ($GITHUB_REF_NAME),
# making the release if it is not there yet: the Switch's and Android's
# workflows each publish their own file, whichever finishes first.
set -euo pipefail
file="$1"
repo="$GITHUB_REPOSITORY"
tag="$GITHUB_REF_NAME"

# Versions before 1.0 are published as pre-releases.
prerelease=""
case "$tag" in v0.*) prerelease="--prerelease" ;; esac
notes="OpenAoK for the Nintendo Switch (OpenAoK-switch-$tag.zip) and Android (OpenAoK-$tag.apk), \
neither of which holds any of the game. Make the game's data from your own CD with this release's \
source code: copy the CD into original_game_1999/, then run python3 switch/package.py --out followed \
by the SD card's folder, or python3 android/package.py and copy the OpenAoK-data folder it makes to \
the phone. The README's \"On the Nintendo Switch\" and \"On Android\" have the steps."

if gh release view "$tag" --repo "$repo" >/dev/null 2>&1; then
    gh release upload "$tag" "$file" --repo "$repo" --clobber
elif ! gh release create "$tag" "$file" --repo "$repo" --title "OpenAoK $tag" $prerelease --notes "$notes"; then
    # Made by the other workflow in the meantime.
    gh release upload "$tag" "$file" --repo "$repo" --clobber
fi

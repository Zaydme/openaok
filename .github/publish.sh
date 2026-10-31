#!/usr/bin/env bash
# Adds FILE to the release of the version tag being built ($GITHUB_REF_NAME),
# making the release if it is not there yet: the Switch's and Android's
# workflows each publish their own file, whichever finishes first. The
# release's notes are written for it afterwards.
set -euo pipefail
file="$1"
repo="$GITHUB_REPOSITORY"
tag="$GITHUB_REF_NAME"

# Versions before 1.0 are published as pre-releases.
prerelease=""
case "$tag" in v0.*) prerelease="--prerelease" ;; esac

if gh release view "$tag" --repo "$repo" >/dev/null 2>&1; then
    gh release upload "$tag" "$file" --repo "$repo" --clobber
elif ! gh release create "$tag" "$file" --repo "$repo" --title "OpenAoK $tag" $prerelease --notes ""; then
    # Made by the other workflow in the meantime.
    gh release upload "$tag" "$file" --repo "$repo" --clobber
fi

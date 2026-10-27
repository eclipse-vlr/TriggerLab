#!/usr/bin/env bash
set -euo pipefail
version=$(sed -nE 's/^project\(TriggerLab VERSION ([0-9]+\.[0-9]+\.[0-9]+) LANGUAGES CXX\)$/\1/p' CMakeLists.txt)
test -n "$version"
tag="v$version"
state=$(gh release view "$tag" --json isDraft,targetCommitish 2>/dev/null || true)
if test -n "$state"; then
  draft=$(printf '%s' "$state" | python3 -c 'import json,sys; print(str(json.load(sys.stdin)["isDraft"]).lower())')
  if test "$draft" = false; then
    echo "Published release $tag already exists."
    exit 0
  fi
  target=$(printf '%s' "$state" | python3 -c 'import json,sys; print(json.load(sys.stdin)["targetCommitish"])')
  test "$target" = "$GITHUB_SHA" || { echo "Draft belongs to another commit." >&2; exit 1; }
fi
test -f "docs/releases/$version.md"
gh run download "$GITHUB_RUN_ID" --name TriggerLab-portable-preview --dir release-assets
archive="TriggerLab-$version-windows-x64.zip"
(cd release-assets && sha256sum --check "$archive.sha256")
if test -z "$state"; then
  gh release create "$tag" --target "$GITHUB_SHA" --title "TriggerLab $version" --notes-file "docs/releases/$version.md" --draft
fi
gh release upload "$tag" "release-assets/$archive" "release-assets/$archive.sha256" --clobber
gh release edit "$tag" --draft=false --latest

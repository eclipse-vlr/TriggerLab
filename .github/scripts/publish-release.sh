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
# matching-refs returns [] for an absent tag and fails on API/auth errors.
# Resolve annotated tags explicitly; --target never validates an existing tag.
tag_objects=$(gh api "repos/$GH_REPO/git/matching-refs/tags/$tag")
tag_object=$(printf '%s' "$tag_objects" | python3 -c '
import json,sys
matches=[item["object"] for item in json.load(sys.stdin) if item["ref"] == "refs/tags/" + sys.argv[1]]
if len(matches) > 1:
    raise SystemExit("Ambiguous release tag")
if matches:
    print(matches[0]["type"], matches[0]["sha"])
' "$tag")
if test -n "$tag_object"; then
  read -r object_type object_sha <<< "$tag_object"
  for depth in {1..16}; do
    test "$object_type" = tag || break
    tag_object=$(gh api "repos/$GH_REPO/git/tags/$object_sha" --jq '.object.type + " " + .object.sha')
    read -r object_type object_sha <<< "$tag_object"
  done
  test "$object_type" = commit && test "$object_sha" = "$GITHUB_SHA" || {
    echo "Existing release tag $tag does not resolve to $GITHUB_SHA; nothing published." >&2
    exit 1
  }
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

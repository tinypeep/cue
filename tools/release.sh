#!/usr/bin/env bash
# Publish a GitHub release used by Cue's updater.
# 1. Update src/core/version.h.
# 2. Place the matching libmpv-2.dll in build/Cue/.
# 3. Run bash tools/release.sh (make, MinGW-w64, Python 3, authenticated gh).
# Builds the installer, attaches matching engine sources, and uses GitHub's
# generated source archives for Cue. GitHub asset SHA-256 is checked by the updater.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^#define CUE_VERSION "\(.*\)"/\1/p' src/core/version.h)
REPO=$(sed -n 's/^#define CUE_UPDATE_REPO "\(.*\)"/\1/p' src/core/version.h)
ENGINE_SRC=${ENGINE_SRC:-sources/Cue-Engine-Source.zip}
echo "Cue $VERSION -> github.com/$REPO"

bash tools/test.sh
make -j"$(nproc)"
make setup
mkdir -p dist
NOTES=$(awk "/^## $VERSION/{f=1;next}/^## /{f=0}f" docs/CHANGELOG.md)
ASSETS=("dist/Cue-$VERSION-Setup.exe")
if [[ -f "$ENGINE_SRC" ]]; then cp "$ENGINE_SRC" "dist/Cue-$VERSION-Engine-Source.zip"; ASSETS+=("dist/Cue-$VERSION-Engine-Source.zip")
else echo "warning: $ENGINE_SRC not found (attach matching engine sources to the release)"; fi
gh release create "v$VERSION" "${ASSETS[@]}" --repo "$REPO" --title "Cue $VERSION" --notes "$NOTES"
echo "published: https://github.com/$REPO/releases/tag/v$VERSION"

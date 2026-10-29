#!/usr/bin/env bash
# Publica uma versão no GitHub Releases. É daí que o Cue instalado puxa as atualizações.
#
#   1. Suba o número em src/core/version.h (CUE_VERSION etc.)
#   2. Coloque libmpv-2.dll em build/Cue/ (do pacote 1.0 ou tools/fetch-libmpv.ps1)
#   3. bash tools/release.sh            (precisa de: make, x86_64-w64-mingw32-g++, python3, gh autenticado)
#
# Gera dist/Cue-<versão>-Setup.exe e cria a release v<versão> com ele + o pacote de fontes do motor
# (Cue-<versão>-Engine-Source.zip). O código do Cue é o "Source code" que o GitHub gera para a tag.
# O GitHub calcula o SHA-256 de cada arquivo enviado; o Cue confere esse valor antes de instalar.
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION=$(sed -n 's/^#define CUE_VERSION "\(.*\)"/\1/p' src/core/version.h)
REPO=$(sed -n 's/^#define CUE_UPDATE_REPO "\(.*\)"/\1/p' src/core/version.h)
ENGINE_SRC=${ENGINE_SRC:-fontes/Cue-motor-fontes.zip}
echo "Cue $VERSION -> github.com/$REPO"

bash tools/test.sh
make -j"$(nproc)"
make setup
mkdir -p dist
NOTES=$(awk "/^## $VERSION/{f=1;next}/^## /{f=0}f" docs/CHANGELOG.md)
ASSETS=("dist/Cue-$VERSION-Setup.exe")
if [[ -f "$ENGINE_SRC" ]]; then cp "$ENGINE_SRC" "dist/Cue-$VERSION-Engine-Source.zip"; ASSETS+=("dist/Cue-$VERSION-Engine-Source.zip")
else echo "aviso: $ENGINE_SRC não encontrado (anexe as fontes do motor à release)"; fi
gh release create "v$VERSION" "${ASSETS[@]}" --repo "$REPO" --title "Cue $VERSION" --notes "$NOTES"
echo "publicado: https://github.com/$REPO/releases/tag/v$VERSION"

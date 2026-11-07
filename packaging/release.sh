#!/usr/bin/env bash
# =============================================================================
#  release.sh — gera os pacotes locais do Kai para Windows (sem git, tag ou push).
#
#  Builda via docker compose (cross-compile) e deixa em dist/:
#    kai-<versão>-setup.exe           instalador NSIS
#    kai-<versão>-windows-x86_64.zip  pasta portátil
#    SHA256SUMS
#
#  A versão vem do CMakeLists.txt (project VERSION + KAI_VERSION_PRERELEASE).
#  Para mudar a versão, edite essas duas linhas; publicar é com tag + CI.
#
#  Uso: ./packaging/release.sh
# =============================================================================
set -euo pipefail

cd "$(git rev-parse --show-toplevel)"

core="$(grep -oP 'project\(kai VERSION \K[0-9]+\.[0-9]+\.[0-9]+' CMakeLists.txt)"
pre="$(grep -oP 'set\(KAI_VERSION_PRERELEASE "\K[^"]*' CMakeLists.txt)"
VERSION="${core}${pre:+-$pre}"

SETUP="dist/kai-${VERSION}-setup.exe"
ZIP="dist/kai-${VERSION}-windows-x86_64.zip"

echo "==> Kai ${VERSION} (Windows)"

docker compose build build-windows
KAI_BUILD_INSTALLER=1 docker compose run --rm build-windows

[[ -f dist/kai-setup.exe ]] || { echo "[ERRO] dist/kai-setup.exe não encontrado após o build." >&2; exit 1; }
[[ -d dist/kai-windows ]] || { echo "[ERRO] dist/kai-windows/ não encontrado após o build." >&2; exit 1; }

cp -f dist/kai-setup.exe "$SETUP"

rm -f "$ZIP"
python3 - "$ZIP" <<'PY'
import os, sys, zipfile
base = "dist/kai-windows"
with zipfile.ZipFile(sys.argv[1], "w", zipfile.ZIP_DEFLATED) as zf:
    for root, _dirs, files in os.walk(base):
        for f in files:
            full = os.path.join(root, f)
            zf.write(full, os.path.relpath(full, base))
PY

(cd dist && sha256sum "$(basename "$SETUP")" "$(basename "$ZIP")" > SHA256SUMS)

echo "==> Pronto:"
ls -lh "$SETUP" "$ZIP" dist/SHA256SUMS

#!/usr/bin/env bash
# =============================================================================
#  build-linux-entrypoint.sh — Compila o Kai e empacota artefatos Linux.
#  Executado dentro do container kai-build-linux. Copia o binário e, se o
#  linuxdeploy estiver disponível/baixável, um AppImage para /out.
# =============================================================================
set -euo pipefail

SRC=/src
BUILD_DIR=/tmp/build-linux
OUT=/out

echo "==> Configurando (CMake, Release, Ninja)..."
cmake -B "$BUILD_DIR" -S "$SRC" -G Ninja -DCMAKE_BUILD_TYPE=Release

echo "==> Compilando..."
cmake --build "$BUILD_DIR" --parallel "$(nproc)"

BIN="$BUILD_DIR/bin/kai"
if [[ ! -x "$BIN" ]]; then
    echo "[ERRO] Binário não encontrado em $BIN" >&2
    exit 1
fi

mkdir -p "$OUT"

# --- AppImage (portável) ---------------------------------------------------
# Monta um AppDir padrão e usa linuxdeploy + o plugin do Qt para agregar as
# dependências. Se o download das ferramentas falhar (ex: build offline),
# cai para copiar apenas o binário + assets, sem abortar o build.
APPDIR="$BUILD_DIR/AppDir"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps"

cp "$BIN" "$APPDIR/usr/bin/kai"
cp -r "$SRC/assets" "$APPDIR/usr/bin/assets"

# .desktop e ícone exigidos pelo formato AppImage.
cat > "$APPDIR/usr/share/applications/kai.desktop" <<'DESKTOP'
[Desktop Entry]
Type=Application
Name=Kai
Comment=Native developer command runner
Exec=kai
Icon=kai
Categories=Development;Utility;
Terminal=false
DESKTOP
cp "$APPDIR/usr/share/applications/kai.desktop" "$APPDIR/kai.desktop"

if [[ -f "$SRC/assets/logo/kai.png" ]]; then
    cp "$SRC/assets/logo/kai.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/kai.png"
    cp "$SRC/assets/logo/kai.png" "$APPDIR/kai.png"
fi

APPIMAGE_OK=0
TOOLS_DIR="$BUILD_DIR/tools"
mkdir -p "$TOOLS_DIR"
LD="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
LDQT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"

if curl -fsSL -o "$LD" \
      "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" \
   && curl -fsSL -o "$LDQT" \
      "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"; then
    chmod +x "$LD" "$LDQT"
    export PATH="$TOOLS_DIR:$PATH"
    export OUTPUT="$OUT/Kai-x86_64.AppImage"

    export APPIMAGE_EXTRACT_AND_RUN=1
    export QMAKE=qmake6

    # --appimage-extract-and-run evita a necessidade de FUSE no container.
    if "$LD" --appimage-extract-and-run \
            --appdir "$APPDIR" \
            --plugin qt \
            --output appimage; then
        # linuxdeploy escreve o AppImage no CWD; move para /out se preciso.
        mv ./Kai*.AppImage "$OUT/" 2>/dev/null || true
        APPIMAGE_OK=1
    fi
fi

if [[ "$APPIMAGE_OK" -eq 1 ]]; then
    echo "==> AppImage gerado em $OUT/"
else
    echo "==> linuxdeploy indisponível/falhou; copiando binário portável."
    mkdir -p "$OUT/kai-linux"
    cp "$BIN" "$OUT/kai-linux/kai"
    cp -r "$SRC/assets" "$OUT/kai-linux/assets"
fi

echo "==> Artefatos Linux em $OUT:"
ls -la "$OUT"

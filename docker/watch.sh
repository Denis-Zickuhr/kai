#!/usr/bin/env bash
set -e

BUILD_DIR="/workspace/dist/dev"
APP_BIN="$BUILD_DIR/bin/kai"

echo "[Kai Watch] Compilando projeto em $BUILD_DIR..."
cmake --build "$BUILD_DIR"

if [ ! -f "$APP_BIN" ]; then
  echo "[Kai Watch Error] Executável não encontrado em $APP_BIN"
  exit 1
fi

echo "[Kai Watch] Iniciando aplicação ($APP_BIN) e aguardando alterações..."

# Roda o app e recompila automaticamente ao alterar arquivos em src/ ou CMakeLists.txt
while true; do
  find /workspace/src /workspace/CMakeLists.txt | entr -r bash -c "cmake --build '$BUILD_DIR' && '$APP_BIN'"
done
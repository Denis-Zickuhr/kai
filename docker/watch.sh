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

# KAI_FORCE_GUI: este loop relança "$APP_BIN" sem argumento nenhum toda vez
# que algo muda, dentro de um pty de verdade (o terminal onde este script
# foi chamado) — sem isto, o `kai` solto acaba do lado do "discover
# automático" (feature nova: `kai` num terminal interativo mostra ajuda/
# comandos em vez de abrir a GUI) e a janela nunca abre (bug real
# reportado: "subi o ctn e não abriu o app"). Esta variável avisa que ESTA
# invocação específica é sempre abertura de GUI, mesmo com tty atrás.
export KAI_FORCE_GUI=1

# Roda o app e recompila automaticamente ao alterar arquivos em src/ ou CMakeLists.txt
while true; do
  find /workspace/src /workspace/CMakeLists.txt | entr -r bash -c "cmake --build '$BUILD_DIR' && '$APP_BIN'"
done
#!/usr/bin/env bash
set -e

BUILD_DIR="/workspace/dist/dev"
APP_BIN="$BUILD_DIR/bin/kai"

# Um único ponto de saída com hora, para dar pra ver o que aconteceu e quando.
log() { printf '\033[1;36m[Kai Watch %s]\033[0m %s\n' "$(date +%H:%M:%S)" "$*"; }

# KAI_FORCE_GUI: este loop relança "$APP_BIN" sem argumento nenhum toda vez
# que algo muda, dentro de um pty de verdade (o terminal onde este script
# foi chamado) — sem isto, o `kai` solto acaba do lado do "discover
# automático" (feature nova: `kai` num terminal interativo mostra ajuda/
# comandos em vez de abrir a GUI) e a janela nunca abre (bug real
# reportado: "subi o ctn e não abriu o app"). Esta variável avisa que ESTA
# invocação específica é sempre abertura de GUI, mesmo com tty atrás.
export KAI_FORCE_GUI=1
# Log do Qt sempre no stderr deste terminal (nunca journald/syslog), com cor no ninja/cmake mesmo atrás do entr.
export QT_FORCE_STDERR_LOGGING=1
export CLICOLOR_FORCE=1
export CMAKE_COLOR_DIAGNOSTICS=ON

build() {
  log "Compilando (alvo kai)..."
  local start=$SECONDS
  if cmake --build "$BUILD_DIR" --target kai; then
    log "Build ok em $((SECONDS - start))s."
  else
    log "Build FALHOU em $((SECONDS - start))s. Aguardando a próxima alteração em src/."
    return 1
  fi
}

# Chamado pelo entr a cada mudança: compila, relança e diz como o app terminou (antes ficava mudo).
cycle() {
  build || return 0
  log "Iniciando $APP_BIN"
  # Linha a linha: stdout em pipe/pty fica em buffer de bloco e o log parecia "seco" até o app fechar.
  stdbuf -oL -eL "$APP_BIN" || log "Kai terminou com código $?."
}

if [ "$1" = "--cycle" ]; then
  cycle
  exit 0
fi

if [ ! -d "$BUILD_DIR" ]; then
  log "Erro: $BUILD_DIR não existe (o entrypoint gera o build)."
  exit 1
fi

log "Observando src/ e CMakeLists.txt (Ctrl+C para sair)."
# -n: não mexe no terminal (o modo interativo do entr altera o tty e pode embaralhar/engolir a saída do app).
# -r: mata o app antes de cada novo ciclo. Arquivo novo/removido em src/ faz o entr sair; o loop o reinicia.
while true; do
  find /workspace/src /workspace/CMakeLists.txt | entr -n -r bash "$0" --cycle || true
  log "Lista de arquivos mudou; reiniciando o observador."
done

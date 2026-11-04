#!/usr/bin/env bash
set -e

BUILD_DIR="build"
INSTALL_DIR="$HOME/.local/bin"
JOBS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

# ./build.sh          -> só o binário `kai` (build/bin/kai), sem os testes.
# ./build.sh tests    -> tudo (kai + suíte de testes) em build-dev/, com as bibliotecas kai-* compartilhadas: cada teste
#                        fica com poucas centenas de KB em vez de ~14 MB. Rodar: ctest --test-dir build-dev
if [ "$1" = "tests" ]; then
    BUILD_DIR="build-dev"
    echo "==> Configurando o projeto (dev: testes + bibliotecas compartilhadas)..."
    cmake -B "$BUILD_DIR" -S . -DCMAKE_BUILD_TYPE=Release -DKAI_SHARED_LIBS=ON
    echo "==> Compilando o Kai e os testes..."
    cmake --build "$BUILD_DIR" --parallel "$JOBS"
    echo "==> Pronto. Binário: $BUILD_DIR/bin/kai. Testes: ctest --test-dir $BUILD_DIR"
    exit 0
fi

echo "==> Configurando o projeto com CMake..."
cmake -B "$BUILD_DIR" -S . -DCMAKE_BUILD_TYPE=Release

echo "==> Compilando o Kai..."
cmake --build "$BUILD_DIR" --parallel "$JOBS" --target kai

if [ "$1" = "install" ]; then
    echo "==> Instalando binário em $INSTALL_DIR..."
    mkdir -p "$INSTALL_DIR"
    cp "$BUILD_DIR/bin/kai" "$INSTALL_DIR/kai"

    # Os assets em DISCO (temas, pacotes de idioma, conteúdo da ajuda) não estão
    # embutidos no binário — só os ícones estão, via .qrc. Sem esta cópia, o
    # binário instalado não os encontra: ele procura em ../../assets relativo a
    # si mesmo, que a partir de ~/.local/bin aponta para fora do projeto. O
    # resultado era ajuda em branco e nenhum tema além do embutido.
    ASSETS_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/kai/assets"
    echo "==> Instalando assets em $ASSETS_DIR..."
    mkdir -p "$ASSETS_DIR"
    # Os samples viram assets/samples: o agente de IA os consulta como referência (read_samples).
    mkdir -p "$ASSETS_DIR"
    rm -rf "$ASSETS_DIR/samples"
    cp -r sample "$ASSETS_DIR/samples"
    for grupo in themes i18n help logo manifesto; do
        if [ -d "assets/$grupo" ]; then
            rm -rf "$ASSETS_DIR/$grupo"
            cp -r "assets/$grupo" "$ASSETS_DIR/$grupo"
        fi
    done

    # Avisos de licença junto da instalação, pela mesma razão do pacote Windows:
    # o QHotkey (BSD-3) é linkado estaticamente e exige o aviso de copyright na
    # distribuição binária.
    SHARE_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/kai"
    for doc in THIRD-PARTY-NOTICES.md LICENSE; do
        [ -f "$doc" ] && cp "$doc" "$SHARE_DIR/$doc"
    done

    echo "==> Kai instalado com sucesso! Certifique-se de que $INSTALL_DIR está no seu PATH."
else
    echo "==> Build concluído! Binário disponível em: $BUILD_DIR/bin/kai"
    echo "==> Para instalar no sistema, rode: ./build.sh install"
fi

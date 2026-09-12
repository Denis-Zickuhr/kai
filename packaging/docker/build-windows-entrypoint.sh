#!/usr/bin/env bash
# =============================================================================
#  build-windows-entrypoint.sh — Cross-compila o Kai para Windows (x86_64)
#  dentro da imagem stateoftheartio/qt6:*-mingw-aqt. Usa qt-cmake (toolchain
#  MinGW+Qt já configurado) e windeployqt (via wine) para empacotar as DLLs
#  e plugins. Resultado: /out/kai-windows/ (kai.exe + Qt DLLs + assets).
# =============================================================================
set -euo pipefail

SRC=/src
BUILD_DIR=/home/user/build
OUT=/out

# O módulo Qt Svg vem COZIDO NA IMAGEM (Dockerfile.windows). Antes era
# instalado via aqt a CADA execução, gastando rede e inflando a camada de
# escrita do container em todo build.
# REDE DE SEGURANÇA: se a imagem em uso for antiga (construída antes dessa
# mudança) ou o aqt tiver falhado no build da imagem, instalamos aqui — assim
# remover a instalação do caminho quente não quebra ninguém.
# $QT_PATH é um caminho WINDOWS ("C:\Qt") para o wine. Do lado Linux ele
# corresponde a $HOME/.wine/drive_c/Qt — usar $QT_PATH direto num `find` do
# Linux sempre falha, e o fallback rodaria à toa em todo build.
QT_LINUX_PATH="${QT_PATH/C:\\/$HOME/.wine/drive_c/}"
QT_LINUX_PATH="${QT_LINUX_PATH//\\//}"
if [[ ! -d "$QT_LINUX_PATH" ]]; then
    QT_LINUX_PATH="$HOME/.wine/drive_c/Qt"
fi
if ! find "$QT_LINUX_PATH" -name 'Qt6Svg*' -print -quit 2>/dev/null | grep -q .; then
    echo "==> Qt Svg ausente na imagem; instalando via aqt (fallback)..."
    aqt install-qt -O "$QT_PATH" all_os windows "$QT_VERSION" win64_mingw -m qtsvg 2>/dev/null \
        || echo "   (aqt falhou; seguindo — qtsvg pode já vir no toolkit base)"
fi

echo "==> Cross-compilando o Kai (qt-cmake + Ninja)..."
# BUILD INCREMENTAL. Antes havia um `rm -rf "$BUILD_DIR"` que forçava recompilar
# as ~227 unidades em TODA execução — a maior fonte de escrita em disco (e, sob
# WSL2, o vhdx do Docker cresce e nunca encolhe). As flags são passadas em todo
# configure, então o cache antigo não "gruda" valores errados; se ainda assim
# quiser começar limpo, rode com KAI_CLEAN_BUILD=1.
if [[ "${KAI_CLEAN_BUILD:-0}" == "1" ]]; then
    echo "   KAI_CLEAN_BUILD=1: descartando o diretório de build."
    rm -rf "$BUILD_DIR"
fi
# FETCHCONTENT_SOURCE_DIR_QHOTKEY/LIBVTERM apontam para o QHotkey/libvterm
# pré-clonados na imagem (o CMake do wine não tem git para clonar durante o
# configure — mesmo problema, mesma solução dos dois).
# SEM ccache aqui: MEDIDO que não funciona neste cross-build. O compilador é um
# executável WINDOWS rodando sob wine (C:\Qt\Tools\mingw1120_64\bin\c++.exe) e o
# ccache do repositório é um binário Linux — ao ser usado como
# CMAKE_CXX_COMPILER_LAUNCHER, ele tenta executar o .exe e falha com
# "CreateProcess failed: The system cannot find the file specified", derrubando
# todo o build. Um ccache do lado Windows seria necessário; o ganho real de
# desempenho aqui vem do build INCREMENTAL (volume kai-win-build).
qt-cmake "$SRC" -G Ninja -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
    -DKAI_BUILD_TESTS=OFF \
    -DFETCHCONTENT_SOURCE_DIR_QHOTKEY=/opt/qhotkey \
    -DFETCHCONTENT_SOURCE_DIR_LIBVTERM=/opt/libvterm
# PARALELISMO DERIVADO DA COTA REAL DE CPU.
# `nproc` NÃO respeita o limite do Docker: com `cpus: 4` num host de 8 núcleos
# ele ainda reporta 8, e o Ninja lançava 8 compiladores — que aqui são processos
# WINE (pesados) — disputando 4 CPUs. Resultado observado pelo usuário: consumo
# de CPU descontrolado durante o build. Lemos a cota do cgroup v2
# (/sys/fs/cgroup/cpu.max = "<quota> <periodo>") e usamos o menor entre ela e o
# nproc. Override manual: KAI_BUILD_JOBS.
JOBS="${KAI_BUILD_JOBS:-}"
if [[ -z "$JOBS" ]]; then
    JOBS="$(nproc)"
    if [[ -r /sys/fs/cgroup/cpu.max ]]; then
        read -r QUOTA PERIOD < /sys/fs/cgroup/cpu.max || true
        if [[ "$QUOTA" != "max" && -n "${PERIOD:-}" && "$PERIOD" -gt 0 ]]; then
            ALLOWED=$(( QUOTA / PERIOD ))
            [[ "$ALLOWED" -lt 1 ]] && ALLOWED=1
            [[ "$ALLOWED" -lt "$JOBS" ]] && JOBS="$ALLOWED"
        fi
    fi
fi
echo "==> Compilando com $JOBS job(s) (cota de CPU respeitada)."
cmake --build "$BUILD_DIR" --parallel "$JOBS"

# Localiza o kai.exe gerado.
EXE=""
for cand in "$BUILD_DIR/bin/kai.exe" "$BUILD_DIR/kai.exe"; do
    if [[ -f "$cand" ]]; then EXE="$cand"; break; fi
done
if [[ -z "$EXE" ]]; then
    echo "[ERRO] kai.exe não encontrado após o build." >&2
    find "$BUILD_DIR" -name '*.exe' -print >&2 || true
    exit 1
fi

DIST="$OUT/kai-windows"
rm -rf "$DIST"
mkdir -p "$DIST"
cp "$EXE" "$DIST/kai.exe"
cp -r "$SRC/assets" "$DIST/assets"

# AVISOS DE LICENÇA junto do binário. Obrigatório, não cortesia: o QHotkey é
# BSD-3 e está linkado ESTATICAMENTE no kai.exe, e a licença exige reproduzir o
# aviso de copyright "in the documentation and/or other materials provided with
# the distribution". O Qt 6 é LGPLv3 e precisa ser declarado. Sem estes arquivos
# no pacote, a distribuição descumpre as duas licenças.
for doc in THIRD-PARTY-NOTICES.md LICENSE; do
    [[ -f "$SRC/$doc" ]] && cp "$SRC/$doc" "$DIST/$doc"
done

echo "==> Empacotando DLLs e plugins do Qt com windeployqt (wine)..."
# windeployqt resolve automaticamente as DLLs do Qt (Core/Gui/Widgets/
# Network/Svg) e os plugins de plataforma/imageformats/svg/tls que o Kai
# carrega em runtime, copiando tudo para junto do kai.exe.
windeployqt --dir "$DIST" --libdir "$DIST" --plugindir "$DIST/plugins" \
    --no-translations --compiler-runtime "$DIST/kai.exe" \
    || echo "   (windeployqt reportou avisos; verifique as DLLs abaixo)"

echo "==> Artefatos Windows em $OUT:"
ls -la "$DIST"

# =============================================================================
#  Geração do INSTALADOR (kai-setup.exe) via Inno Setup 6 (ISCC) sob wine.
#  Best-effort: se o ISCC não estiver disponível na imagem, baixamos o
#  compilador portátil do Inno Setup e o executamos via wine. O resultado
#  final é $OUT/kai-setup.exe. Passamos os diretórios reais via /D para o
#  .iss (KaiSrcDir = pasta portável recém-gerada; KaiOutDir = /out).
# =============================================================================
# OPT-IN: por padrão NÃO geramos o instalador aqui. Este trecho baixava o
# Inno Setup (~5 MB) e o instalava dentro do prefixo wine a CADA execução,
# inflando a camada do container e sendo a parte mais instável do build.
# O instalador oficial é gerado com NSIS (packaging/windows/kai.nsi), fora
# desta imagem. Para reativar, rode com KAI_BUILD_INSTALLER=1.
if [[ "${KAI_BUILD_INSTALLER:-0}" != "1" ]]; then
    echo "==> Instalador via wine DESABILITADO (use KAI_BUILD_INSTALLER=1 para ligar)."
    echo "==> Pronto. Pasta portável em $DIST/"
    exit 0
fi

echo "==> Gerando instalador..."
ISS="$SRC/packaging/windows/kai.iss"
NSI="$SRC/packaging/windows/kai.nsi"
ISCC_EXE=""

# CAMINHO PREFERIDO: NSIS. O makensis é um binário LINUX NATIVO, então não passa
# por wine e não depende de download em tempo de build — as duas coisas que
# tornavam a geração do instalador frágil. O Inno Setup ficou como reserva.
#
# Histórico do porquê: o download versionado do Inno Setup em
# files.jrsoftware.org passou a devolver 404 (a distribuição migrou para releases
# do GitHub) e, mesmo baixando o instalador correto, a instalação silenciosa dele
# sob wine termina com código 0 sem instalar nada — o ISCC.exe simplesmente não
# aparece, sem mensagem de erro.
if command -v makensis >/dev/null 2>&1 && [[ -f "$NSI" ]]; then
    echo "   Usando NSIS (makensis nativo, sem wine)..."
    if makensis -V2 \
        "-DKAISRC=$DIST" \
        "-DKAIOUT=$OUT" \
        "$NSI"; then
        echo "==> Instalador gerado: $OUT/kai-setup.exe"
        ls -la "$OUT"/kai-setup.exe 2>/dev/null || true
        INSTALLER_DONE=1
    else
        echo "   [AVISO] makensis falhou; tentando o Inno Setup como reserva."
    fi
fi

if [[ "${INSTALLER_DONE:-0}" != "1" ]]; then

# 1) ISCC já instalado no prefixo wine?
for cand in \
    "$HOME/.wine/drive_c/Program Files (x86)/Inno Setup 6/ISCC.exe" \
    "/opt/innosetup/ISCC.exe"; do
    if [[ -f "$cand" ]]; then ISCC_EXE="$cand"; break; fi
done

# 2) Se não achou, tenta baixar o Inno Setup e instalar silenciosamente.
if [[ -z "$ISCC_EXE" ]]; then
    echo "   ISCC não encontrado; tentando baixar o Inno Setup..."
    IS_INSTALLER="/tmp/innosetup.exe"
    # A distribuição do Inno Setup MIGROU para releases do GitHub. As URLs
    # antigas em files.jrsoftware.org devolvem 404, e jrsoftware.org/download.php
    # /is.exe devolve a PÁGINA HTML de downloads (10 KB), não o executável — o
    # que fazia o passo "baixar" parecer ter dado certo e só falhar depois, na
    # instalação, sem dizer o motivo.
    # A série 6 é a usada porque o kai.iss declara MinVersion/diretivas dela.
    for IS_URL in \
        "https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe" \
        "https://files.jrsoftware.org/is/6/innosetup-6.2.2.exe"; do
        rm -f "$IS_INSTALLER"
        if command -v curl >/dev/null 2>&1; then
            curl -fsSL --max-time 300 -o "$IS_INSTALLER" "$IS_URL" || true
        elif command -v wget >/dev/null 2>&1; then
            wget -qO "$IS_INSTALLER" "$IS_URL" || true
        fi
        # VALIDA que veio um executável Windows, e não uma página de erro: sem
        # esta checagem, um HTML de 10 KB era passado ao wine e a falha aparecia
        # como "Inno Setup indisponível", escondendo a causa real.
        if [[ -s "$IS_INSTALLER" ]] && head -c 2 "$IS_INSTALLER" | grep -q "MZ"; then
            echo "   Inno Setup baixado ($(stat -c%s "$IS_INSTALLER") bytes) de $IS_URL"
            break
        fi
        echo "   [!] $IS_URL não devolveu um executável; tentando o próximo..."
        rm -f "$IS_INSTALLER"
    done
    if [[ -s "$IS_INSTALLER" ]]; then
        echo "   Instalando o Inno Setup no prefixo wine..."
        wine "$IS_INSTALLER" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /SP- 2>/dev/null || true
        for cand in \
            "$HOME/.wine/drive_c/Program Files (x86)/Inno Setup 6/ISCC.exe" \
            "$HOME/.wine/drive_c/Program Files/Inno Setup 6/ISCC.exe"; do
            if [[ -f "$cand" ]]; then ISCC_EXE="$cand"; break; fi
        done
    fi
fi

if [[ -n "$ISCC_EXE" ]]; then
    # Converte paths do container para paths wine (Z: mapeia a raiz do host).
    wine "$ISCC_EXE" \
        "/DKaiSrcDir=Z:${DIST//\//\\}" \
        "/DKaiOutDir=Z:${OUT//\//\\}" \
        "Z:${ISS//\//\\}" 2>/dev/null \
        && echo "==> Instalador gerado: $OUT/kai-setup.exe" \
        || echo "   [AVISO] ISCC falhou; distribua a pasta portável $DIST."
    ls -la "$OUT"/kai-setup.exe 2>/dev/null || true
else
    echo "   [AVISO] Inno Setup indisponível na imagem e download falhou."
    echo "   O instalador não foi gerado automaticamente. Opções:"
    echo "     - Distribua a pasta PORTÁTIL: $DIST (já pronta, kai.exe + DLLs)."
    echo "     - Ou gere o instalador no Windows: iscc packaging\\windows\\kai.iss"
fi

fi  # fim do bloco de reserva do Inno Setup (só roda se o NSIS não gerou)

echo "==> Pronto. Pasta portável em $DIST/ e (se gerado) instalador em $OUT/kai-setup.exe"

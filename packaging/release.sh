#!/usr/bin/env bash
# =============================================================================
#  release.sh — orquestra um release do Kai de ponta a ponta, interativo.
#
#  Pergunta o que fazer em cada etapa (nenhuma delas é automática por padrão):
#    1) buildar novos artefatos (Linux e/ou Windows, via docker compose)
#    2) bump de versão (atualiza CMakeLists.txt + packaging/windows/kai.nsi)
#    3) commitar o bump
#    4) criar tag git anotada
#    5) dar push da branch/tag pro origin
#    6) criar a Release no GitHub (gh release create) anexando os artefatos
#
#  Rode de qualquer lugar dentro do repo: ./packaging/release.sh
# =============================================================================
set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

CMAKE_FILE="CMakeLists.txt"
NSI_FILE="packaging/windows/kai.nsi"

# --- helpers -----------------------------------------------------------------

ask_yes_no() {
    # ask_yes_no "pergunta" "default(y|n)"
    local prompt="$1" default="${2:-n}" reply
    local hint="y/N"
    [[ "$default" == "y" ]] && hint="Y/n"
    read -rp "$prompt [$hint] " reply
    reply="${reply:-$default}"
    [[ "$reply" =~ ^[Yy]$ ]]
}

ask_value() {
    # ask_value "pergunta" "default"
    local prompt="$1" default="$2" reply
    read -rp "$prompt [$default] " reply
    echo "${reply:-$default}"
}

current_version() {
    grep -oP 'project\(kai VERSION \K[0-9]+\.[0-9]+\.[0-9]+' "$CMAKE_FILE"
}

next_patch_version() {
    local v="$1" major minor patch
    IFS='.' read -r major minor patch <<< "$v"
    echo "${major}.${minor}.$((patch + 1))"
}

is_semver() {
    [[ "$1" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]
}

log() { echo "==> $*"; }
warn() { echo "[AVISO] $*" >&2; }
die() { echo "[ERRO] $*" >&2; exit 1; }

# --- estado inicial ------------------------------------------------------------

command -v git >/dev/null || die "git não encontrado."

if [[ -n "$(git status --porcelain)" ]]; then
    warn "Há mudanças não commitadas no working tree:"
    git status --short
    ask_yes_no "Continuar mesmo assim?" n || exit 1
fi

VERSION="$(current_version)"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
log "Versão atual: $VERSION  |  branch: $BRANCH"

# =============================================================================
# 1) Bump de versão
# =============================================================================
DID_BUMP=0
NEW_VERSION="$VERSION"

if ask_yes_no "Fazer bump de versão? (atual: $VERSION)" n; then
    SUGGESTED="$(next_patch_version "$VERSION")"
    NEW_VERSION="$(ask_value "Nova versão (semver X.Y.Z)" "$SUGGESTED")"
    is_semver "$NEW_VERSION" || die "Versão inválida: $NEW_VERSION (esperado X.Y.Z)"

    log "Atualizando $CMAKE_FILE e $NSI_FILE para $NEW_VERSION..."
    sed -i "s/project(kai VERSION [0-9]\+\.[0-9]\+\.[0-9]\+/project(kai VERSION ${NEW_VERSION}/" "$CMAKE_FILE"
    sed -i "s/!define APPVERSION \"[0-9]\+\.[0-9]\+\.[0-9]\+\"/!define APPVERSION \"${NEW_VERSION}\"/" "$NSI_FILE"
    sed -i "s/VIProductVersion \"[0-9]\+\.[0-9]\+\.[0-9]\+\.0\"/VIProductVersion \"${NEW_VERSION}.0\"/" "$NSI_FILE"
    sed -i "s/\"ProductVersion\"  *\"[0-9]\+\.[0-9]\+\.[0-9]\+\.0\"/\"ProductVersion\"  \"${NEW_VERSION}.0\"/" "$NSI_FILE"
    sed -i "s/\"FileVersion\"     *\"[0-9]\+\.[0-9]\+\.[0-9]\+\.0\"/\"FileVersion\"     \"${NEW_VERSION}.0\"/" "$NSI_FILE"

    VERSION="$NEW_VERSION"
    DID_BUMP=1
    log "Versão em disco agora: $(current_version)"
    git diff -- "$CMAKE_FILE" "$NSI_FILE"
else
    log "Sem bump — mantendo $VERSION."
fi

# =============================================================================
# 2) Build de artefatos
# =============================================================================
DID_BUILD_LINUX=0
DID_BUILD_WINDOWS=0

if ask_yes_no "Buildar novos artefatos agora?" y; then
    echo "  [1] Linux (AppImage)"
    echo "  [2] Windows (kai.exe + instalador NSIS)"
    echo "  [3] Ambos"
    TARGET="$(ask_value "Alvo do build" "3")"

    if [[ "$TARGET" == "1" || "$TARGET" == "3" ]]; then
        log "Buildando imagem Linux..."
        docker compose build build-linux
        log "Gerando artefato Linux..."
        docker compose run --rm build-linux
        DID_BUILD_LINUX=1
    fi

    if [[ "$TARGET" == "2" || "$TARGET" == "3" ]]; then
        log "Buildando imagem Windows (cross-compile, pode demorar)..."
        docker compose build build-windows
        log "Gerando artefato Windows + instalador..."
        KAI_BUILD_INSTALLER=1 docker compose run --rm build-windows
        DID_BUILD_WINDOWS=1
    fi
else
    log "Pulando build — assumindo que dist/ já tem os artefatos certos para $VERSION."
fi

# --- renomeia/empacota os artefatos com a versão alvo -------------------------

LINUX_ARTIFACT="dist/kai-${VERSION}-linux-x86_64.AppImage"
WINDOWS_SETUP="dist/kai-${VERSION}-setup.exe"
WINDOWS_ZIP="dist/kai-${VERSION}-windows-x86_64.zip"

if [[ "$DID_BUILD_LINUX" == "1" ]]; then
    [[ -f dist/Kai-x86_64.AppImage ]] || die "dist/Kai-x86_64.AppImage não encontrado após o build."
    cp -f dist/Kai-x86_64.AppImage "$LINUX_ARTIFACT"
    log "Artefato Linux: $LINUX_ARTIFACT"
fi

if [[ "$DID_BUILD_WINDOWS" == "1" ]]; then
    [[ -f dist/kai-setup.exe ]] || die "dist/kai-setup.exe não encontrado após o build."
    cp -f dist/kai-setup.exe "$WINDOWS_SETUP"
    log "Instalador Windows: $WINDOWS_SETUP"

    [[ -d dist/kai-windows ]] || die "dist/kai-windows/ não encontrado após o build."
    log "Empacotando pasta portátil em $WINDOWS_ZIP..."
    python3 -c "
import zipfile, os
zf = zipfile.ZipFile('$WINDOWS_ZIP', 'w', zipfile.ZIP_DEFLATED)
base = 'dist/kai-windows'
for root, _dirs, files in os.walk(base):
    for f in files:
        full = os.path.join(root, f)
        arc = os.path.relpath(full, base)
        zf.write(full, arc)
zf.close()
"
    log "Zip portátil: $WINDOWS_ZIP"
fi

ARTIFACTS=()
[[ -f "$LINUX_ARTIFACT" ]] && ARTIFACTS+=("$LINUX_ARTIFACT")
[[ -f "$WINDOWS_SETUP" ]] && ARTIFACTS+=("$WINDOWS_SETUP")
[[ -f "$WINDOWS_ZIP" ]] && ARTIFACTS+=("$WINDOWS_ZIP")

if [[ "${#ARTIFACTS[@]}" -gt 0 ]]; then
    log "Artefatos prontos para $VERSION:"
    printf '   %s\n' "${ARTIFACTS[@]}"
fi

# =============================================================================
# 3) Commit do bump
# =============================================================================
if [[ "$DID_BUMP" == "1" ]]; then
    if ask_yes_no "Commitar o bump de versão ($VERSION) na branch '$BRANCH'?" y; then
        git add "$CMAKE_FILE" "$NSI_FILE"
        git commit -m "chore(release): bump version to ${VERSION}"
        log "Commit criado: $(git rev-parse --short HEAD)"
    else
        warn "Bump feito em disco mas NÃO commitado — o working tree ficou sujo."
    fi
fi

# =============================================================================
# 4) Tag git
# =============================================================================
TAG=""
if ask_yes_no "Criar tag git para este release?" "$([[ $DID_BUMP == 1 ]] && echo y || echo n)"; then
    TAG="$(ask_value "Nome da tag" "v${VERSION}")"
    if git rev-parse "$TAG" >/dev/null 2>&1; then
        die "Tag '$TAG' já existe localmente."
    fi
    git tag -a "$TAG" -m "Kai ${VERSION}"
    log "Tag criada: $TAG (em $(git rev-parse --short HEAD))"
fi

# =============================================================================
# 5) Push
# =============================================================================
if [[ -n "$TAG" ]] || [[ "$DID_BUMP" == "1" ]]; then
    if ask_yes_no "Dar push de '$BRANCH'${TAG:+ e da tag '$TAG'} para origin?" n; then
        git push origin "$BRANCH"
        [[ -n "$TAG" ]] && git push origin "$TAG"
        log "Push concluído."
    else
        warn "Nada enviado ao origin — branch/tag continuam só locais."
    fi
fi

# =============================================================================
# 6) GitHub Release
# =============================================================================
# Tag pra mirar na Release do GitHub: a que acabou de ser criada aqui, OU
# (se não criamos tag nesta rodada, ex: só rebuild sem bump) uma existente
# — pedida na hora, com a v${VERSION} atual como sugestão.
RELEASE_TAG="$TAG"
if [[ -z "$RELEASE_TAG" ]] && command -v gh >/dev/null 2>&1; then
    if ask_yes_no "Atualizar/criar Release no GitHub para a versão ${VERSION}?" n; then
        RELEASE_TAG="$(ask_value "Tag da Release" "v${VERSION}")"
    fi
elif [[ -n "$RELEASE_TAG" ]] && command -v gh >/dev/null 2>&1; then
    ask_yes_no "Publicar/atualizar Release no GitHub para '$RELEASE_TAG'?" n || RELEASE_TAG=""
fi

if [[ -n "$RELEASE_TAG" ]] && command -v gh >/dev/null 2>&1; then
    if ! git ls-remote --tags origin | grep -q "refs/tags/${RELEASE_TAG}$"; then
        if git rev-parse "$RELEASE_TAG" >/dev/null 2>&1; then
            warn "Tag '$RELEASE_TAG' ainda não está no origin — dando push dela agora."
            git push origin "$RELEASE_TAG"
        else
            die "Tag '$RELEASE_TAG' não existe local nem remotamente. Crie a tag antes (etapa anterior) ou informe uma tag existente."
        fi
    fi

    if [[ "${#ARTIFACTS[@]}" -eq 0 ]]; then
        warn "Nenhum artefato em mãos para anexar (pulou o build?)."
    fi

    if gh release view "$RELEASE_TAG" >/dev/null 2>&1; then
        # Release já existe (caso comum: rebuild dos binários de uma versão
        # já publicada, sem bump nem tag nova) — SUBSTITUI os assets em vez
        # de tentar criar de novo (gh release create falharia com "already
        # exists"). --clobber sobrescreve cada asset de mesmo nome.
        log "Release '$RELEASE_TAG' já existe — atualizando assets (--clobber)."
        if [[ "${#ARTIFACTS[@]}" -gt 0 ]]; then
            gh release upload "$RELEASE_TAG" "${ARTIFACTS[@]}" --clobber
        fi
    else
        gh release create "$RELEASE_TAG" "${ARTIFACTS[@]}" \
            --title "Kai ${VERSION}" \
            --notes "Release ${VERSION}."
    fi
    log "Release: $(gh release view "$RELEASE_TAG" --json url -q .url)"
elif [[ -n "$RELEASE_TAG" ]]; then
    warn "gh CLI não encontrado — pulei a publicação da Release no GitHub."
fi

log "Concluído."

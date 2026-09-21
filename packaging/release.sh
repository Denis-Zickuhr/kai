#!/usr/bin/env bash
# =============================================================================
#  release.sh — orquestra um release do Kai de ponta a ponta, interativo.
#
#  Faz TODAS as perguntas primeiro, mostra o plano e pede uma confirmação;
#  só então executa, sem perguntar mais nada (dá pra deixar rodando o build
#  longo sem ficar de olho). Etapas (nenhuma é automática por padrão):
#    1) buildar novos artefatos (Linux e/ou Windows, via docker compose)
#    2) bump de versão (atualiza CMakeLists.txt + packaging/windows/kai.nsi)
#    3) commitar o bump
#    4) criar tag git anotada
#    5) dar push da branch/tag pro origin
#    6) criar a Release no GitHub (gh release create) anexando os artefatos
#
#  Rode de qualquer lugar dentro do repo: ./packaging/release.sh
#
#  Modo KIP (--kip): o mesmo fluxo, mas falando o Kai Interface Protocol — o Kai
#  mostra um formulário no lugar das perguntas, o plano numa confirmação (vermelha
#  quando há push ou Release) e a execução como uma checklist. Use com
#  `kip: true` no comando do Kai. Flags de resposta (--bump=..., --build=...)
#  viram os valores iniciais do formulário. Sem --kip nada muda: tudo continua
#  no terminal. Protocolo na mão (sem jq nem o helper `kai kip`); todo texto que
#  não é protocolo — logs, saída do git/docker — vai para o stderr.
#
#  Respostas prévias por flag (o que não vier por flag é perguntado):
#    --bump=X.Y.Z | --bump=patch | --no-bump (= --bump=none)
#    --build=linux|windows|both|none
#    --commit | --no-commit | --commit=yes|no   (commit do bump)
#    --tag | --tag=NOME | --no-tag (= --tag=none; --tag sozinho = vX.Y.Z)
#    --push | --no-push | --push=yes|no
#    --release | --release=TAG | --no-release (= --release=none)
#    --allow-dirty                     (não pergunta sobre mudanças pendentes)
#    -y | --yes                        (pula a confirmação final)
#    --kip                             (modo KIP: a interface do Kai no lugar das perguntas)
#  Ex.: ./packaging/release.sh --bump=patch --build=both --commit --tag --push --release -y
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
die() {
    [[ "${KIP_READY:-0}" == "1" ]] && kip_die "$*"
    echo "[ERRO] $*" >&2
    exit 1
}

# Executa um comando. No terminal é só executar; no modo KIP roda em segundo plano
# e espera, para o SIGTERM do Cancelar do Kai conseguir matar o filho (o bash só
# trata o trap depois de um comando em primeiro plano) e sem ler o stdin do protocolo.
run_cmd() {
    if [[ "${KIP_READY:-0}" != "1" ]]; then
        "$@"
        return
    fi
    "$@" </dev/null &
    KIP_CHILD=$!
    local rc=0
    wait "$KIP_CHILD" || rc=$?
    KIP_CHILD=""
    return "$rc"
}

# =============================================================================
#  MODO KIP (--kip)
# =============================================================================
KIP_LOG=""; KIP_ERRFILE=""; KIP_CHILD=""; KIP_STEP=""; KIP_PROBE=0; RELEASE_URL=""

# Uma linha de JSON no canal do protocolo (fd 3). Estilo printf: send '{"a":"%s"}' "$(esc "$v")".
send() { printf "$1" "${@:2}" | tr -d '\n' >&3; echo >&3; }
esc() { local s=${1//\\/\\\\}; s=${s//\"/\\\"}; s=${s//$'\n'/\\n}; printf '%s' "${s//$'\t'/\\t}"; }
jget() {
    local v
    v=$(printf '%s' "$MSG" | sed -nE 's/.*"'"$1"'":"(([^"\\]|\\.)*)".*/\1/p')
    [ -z "$v" ] && v=$(printf '%s' "$MSG" | sed -nE 's/.*"'"$1"'":(true|false|null|-?[0-9.]+).*/\1/p')
    v=${v//\\\\/$'\001'}; v=${v//\\\"/\"}; v=${v//\\\//\/}; v=${v//\\n/$'\n'}; v=${v//$'\001'/\\}
    printf '%s' "$v"
}
recv() { IFS= read -r MSG || exit 130; [[ "$(jget type)" == cancel ]] && exit 130; return 0; }
# Texto na língua do Kai (KIP_LOCALE): L "english" "português".
L() { if [[ "${KIP_LOCALE:-en}" == "pt" ]]; then printf '%s' "$2"; else printf '%s' "$1"; fi; }

# Abre o canal: o protocolo vai no fd 3; TUDO o mais (log, git, docker) vai para o
# stderr — que o Kai mostra em Detalhes → Log — e também para um arquivo, de onde sai a
# cauda do cartão de erro.
kip_init() {
    KIP_LOG="$(mktemp)"
    KIP_ERRFILE="$(mktemp)"
    exec 3>&1
    exec > >(tee -a "$KIP_LOG" >&2) 2>&1
    DC_T="-T"
    export GIT_TERMINAL_PROMPT=0
    trap 'kip_terminated' TERM INT HUP
    KIP_READY=1
}

kip_terminated() {
    [[ -n "$KIP_CHILD" ]] && kill "$KIP_CHILD" 2>/dev/null
    exit 143
}

# Erro fatal (die): na fase de validação (probe) só guarda o motivo; fora dela encerra
# com um cartão de erro.
kip_die() {
    if [[ "$KIP_PROBE" == "1" ]]; then
        printf '%s' "$*" > "$KIP_ERRFILE"
        exit 1
    fi
    trap - ERR
    send '{"kip":1,"type":"done","level":"error","title":"%s","text":"%s"}' \
        "$(esc "$(L 'Release stopped' 'Release interrompida')")" "$(esc "$*")"
    exit 1
}

kip_begin() {
    kip_init
    send '{"kip":1,"type":"hello","title":"Kai release","version":"%s"}' "$(esc "$(current_version)")"
    # Mudanças pendentes: no terminal pergunta "Continuar mesmo assim?"; aqui é uma confirmação.
    if [[ -n "$(git status --porcelain)" && "$ARG_DIRTY" != "yes" ]]; then
        local changed
        changed="$(git status --short | head -n 20)"
        send '{"kip":1,"type":"confirm","id":"dirty","title":"%s","text":"%s","confirm_label":"%s","cancel_label":"%s"}' \
            "$(esc "$(L 'Uncommitted changes' 'Mudanças não commitadas')")" \
            "$(esc "$(L 'The working tree has changes that are not committed:' 'O working tree tem mudanças não commitadas:')"$'\n\n'"$changed")" \
            "$(esc "$(L 'Continue anyway' 'Continuar mesmo assim')")" "$(esc "$(L 'Stop' 'Parar')")"
        recv
        if [[ "$(jget confirmed)" != true ]]; then
            send '{"kip":1,"type":"done","level":"info","title":"%s","text":"%s"}' \
                "$(esc "$(L 'Stopped' 'Interrompido')")" "$(esc "$(L 'Nothing was changed.' 'Nada foi alterado.')")"
            exit 0
        fi
        ARG_DIRTY="yes"
    fi
}

# O plano em texto simples, para a confirmação do Kai.
kip_plan_text() {
    local yn_yes yn_no build_desc="$(L 'no' 'não')"
    yn_yes="$(L 'yes' 'sim')"; yn_no="$(L 'no' 'não')"
    if [[ "$DO_BUMP" == "1" ]]; then
        printf '%s: %s -> %s\n' "$(L 'Version bump' 'Bump de versão')" "$CURRENT_VERSION" "$VERSION"
    else
        printf '%s: %s (%s)\n' "$(L 'Version bump' 'Bump de versão')" "$yn_no" "$VERSION"
    fi
    [[ "$BUILD_LINUX" == "1" && "$BUILD_WINDOWS" == "1" ]] && build_desc="Linux + Windows"
    [[ "$BUILD_LINUX" == "1" && "$BUILD_WINDOWS" == "0" ]] && build_desc="Linux"
    [[ "$BUILD_LINUX" == "0" && "$BUILD_WINDOWS" == "1" ]] && build_desc="Windows"
    printf '%s: %s\n' "$(L 'Build' 'Build')" "$build_desc"
    [[ "$DO_BUMP" == "1" ]] && printf '%s: %s\n' "$(L 'Commit the bump' 'Commit do bump')" "$([[ $DO_COMMIT == 1 ]] && echo "$yn_yes" || echo "$yn_no")"
    printf '%s: %s\n' "Tag" "${TAG:-$yn_no}"
    [[ -n "$TAG" || "$DO_BUMP" == "1" ]] && printf '%s: %s\n' "$(L 'Push to origin' 'Push para o origin')" "$([[ $DO_PUSH == 1 ]] && echo "$yn_yes" || echo "$yn_no")"
    printf '%s: %s\n' "$(L 'GitHub Release' 'Release no GitHub')" "${RELEASE_TAG:-$yn_no}"
}

# Formulário -> ARG_* (as MESMAS variáveis que as flags/perguntas do terminal preenchem) ->
# validação -> plano -> confirmação. Voltar na confirmação reabre o formulário.
kip_collect() {
    # Valores iniciais: o que veio por flag, senão os mesmos padrões das perguntas do terminal.
    local d_bump=none d_version="" d_build="both" d_commit=true d_tag=true d_push=false d_release=false d_tagname="" d_reltag=""
    case "$ARG_BUMP" in ""|none) ;; patch) d_bump=patch ;; *) d_bump=custom; d_version="$ARG_BUMP" ;; esac
    [[ -n "$ARG_BUILD" ]] && d_build="$ARG_BUILD"
    [[ "$ARG_COMMIT" == "no" ]] && d_commit=false
    case "$ARG_TAG" in "") [[ "$d_bump" == none ]] && d_tag=false ;; none) d_tag=false ;; auto) ;; *) d_tagname="$ARG_TAG" ;; esac
    [[ "$ARG_PUSH" == "yes" ]] && d_push=true
    case "$ARG_RELEASE" in ""|none) ;; auto) d_release=true ;; *) d_release=true; d_reltag="$ARG_RELEASE" ;; esac

    local bump version build commit tag push release tagname reltag err field
    while true; do
        send '{"kip":1,"type":"prompt","id":"release","title":"%s","submit_label":"%s",
            "description":"%s",
            "fields":[
                {"name":"bump","type":"select","label":"%s","required":true,"default":"%s",
                 "options":[{"value":"none","label":"%s"},{"value":"patch","label":"%s"},{"value":"custom","label":"%s"}]},
                {"name":"version","type":"text","label":"%s","placeholder":"X.Y.Z","default":"%s"},
                {"name":"build","type":"select","label":"%s","required":true,"default":"%s",
                 "options":[{"value":"none","label":"%s","description":"%s"},{"value":"linux","label":"Linux (AppImage)"},
                            {"value":"windows","label":"Windows (kai.exe + NSIS)"},{"value":"both","label":"%s"}]},
                {"name":"steps","type":"flags","label":"%s",
                 "options":[{"name":"commit","label":"%s","default":%s},
                            {"name":"tag","label":"%s","default":%s},
                            {"name":"push","label":"%s","description":"%s","default":%s},
                            {"name":"release","label":"%s","description":"%s","default":%s}]},
                {"name":"tag_name","type":"text","label":"%s","group":"%s","default":"%s","placeholder":"v1.2.3"},
                {"name":"release_tag","type":"text","label":"%s","group":"%s","default":"%s","placeholder":"v1.2.3"}
            ]}' \
            "$(esc "$(L 'Publish a release' 'Publicar uma release')")" "$(esc "$(L 'Review the plan' 'Ver o plano')")" \
            "$(esc "$(L "Branch ${BRANCH} · current version ${CURRENT_VERSION}. Nothing changes until you confirm the plan." "Branch ${BRANCH} · versão atual ${CURRENT_VERSION}. Nada muda até você confirmar o plano.")")" \
            "$(esc "$(L 'Version bump' 'Bump de versão')")" "$d_bump" \
            "$(esc "$(L "Keep ${CURRENT_VERSION}" "Manter ${CURRENT_VERSION}")")" \
            "$(esc "$(L "Patch (-> $(next_patch_version "$CURRENT_VERSION"))" "Patch (-> $(next_patch_version "$CURRENT_VERSION"))")")" \
            "$(esc "$(L 'Custom version…' 'Outra versão…')")" \
            "$(esc "$(L 'Custom version (X.Y.Z)' 'Outra versão (X.Y.Z)')")" "$(esc "$d_version")" \
            "$(esc "$(L 'Build artifacts' 'Buildar artefatos')")" "$d_build" \
            "$(esc "$(L 'No build' 'Sem build')")" "$(esc "$(L 'Assume dist/ already has the right files' 'Assume que dist/ já tem os arquivos certos')")" \
            "$(esc "$(L 'Linux + Windows' 'Linux + Windows')")" \
            "$(esc "$(L 'What to do' 'O que fazer')")" \
            "$(esc "$(L 'Commit the version bump' 'Commitar o bump de versão')")" "$d_commit" \
            "$(esc "$(L 'Create an annotated git tag' 'Criar tag git anotada')")" "$d_tag" \
            "$(esc "$(L 'Push the branch and tag to origin' 'Dar push da branch e da tag no origin')")" \
            "$(esc "$(L 'Publishes your commits.' 'Publica os seus commits.')")" "$d_push" \
            "$(esc "$(L 'Publish the GitHub Release' 'Publicar a Release no GitHub')")" \
            "$(esc "$(L 'Needs the gh CLI.' 'Precisa do gh CLI.')")" "$d_release" \
            "$(esc "$(L 'Tag name (empty = v<version>)' 'Nome da tag (vazio = v<versão>)')")" "$(esc "$(L 'Advanced' 'Avançado')")" "$(esc "$d_tagname")" \
            "$(esc "$(L 'Release tag (empty = the tag above)' 'Tag da Release (vazio = a tag acima)')")" "$(esc "$(L 'Advanced' 'Avançado')")" "$(esc "$d_reltag")"

        # "invalid" mantém o MESMO formulário aberto: espera a próxima resposta (não reenvia o prompt).
        while true; do
            recv
            bump=$(jget bump); version=$(jget version); build=$(jget build)
            commit=$(jget commit); tag=$(jget tag); push=$(jget push); release=$(jget release)
            tagname=$(jget tag_name); reltag=$(jget release_tag)

            case "$bump" in
                custom) ARG_BUMP="$version" ;;
                *) ARG_BUMP="$bump" ;;
            esac
            ARG_BUILD="$build"
            if [[ "$bump" != none && "$commit" == true ]]; then ARG_COMMIT="yes"; else ARG_COMMIT="no"; fi
            if [[ "$tag" == true ]]; then ARG_TAG="${tagname:-auto}"; else ARG_TAG="none"; fi
            if [[ "$push" == true ]]; then ARG_PUSH="yes"; else ARG_PUSH="no"; fi
            if [[ "$release" == true ]]; then ARG_RELEASE="${reltag:-auto}"; else ARG_RELEASE="none"; fi

            err=""
            if [[ "$bump" == custom && -z "$version" ]]; then
                err="$(L 'Type the new version (X.Y.Z).' 'Digite a nova versão (X.Y.Z).')"
                field=version
            elif ! ( KIP_PROBE=1; resolve_plan ) >/dev/null 2>&1; then
                # As mesmas validações do terminal (semver, tag existente, gh ausente...): o motivo
                # fica no arquivo de erro. Roda numa subshell: ela não altera o estado daqui.
                err="$(cat "$KIP_ERRFILE")"
                field=""
                [[ "$err" == *"Versão inválida"* ]] && field=version
            fi
            if [[ -n "$err" ]]; then
                if [[ -n "$field" ]]; then
                    send '{"kip":1,"type":"invalid","id":"release","message":"%s","errors":{"%s":"%s"}}' "$(esc "$err")" "$field" "$(esc "$err")"
                else
                    send '{"kip":1,"type":"invalid","id":"release","message":"%s"}' "$(esc "$err")"
                fi
                continue
            fi
            break
        done

        resolve_plan
        local danger=false
        [[ "$DO_PUSH" == "1" || -n "$RELEASE_TAG" ]] && danger=true
        send '{"kip":1,"type":"confirm","id":"plan","title":"%s","text":"%s","danger":%s,"back":true,"confirm_label":"%s","cancel_label":"%s"}' \
            "$(esc "$(L 'Run this release?' 'Executar esta release?')")" "$(esc "$(kip_plan_text)")" "$danger" \
            "$(esc "$(L 'Run release' 'Executar release')")" "$(esc "$(L "Don't run" 'Não executar')")"
        recv
        [[ "$(jget type)" == back ]] && continue
        if [[ "$(jget confirmed)" != true ]]; then
            send '{"kip":1,"type":"done","level":"info","title":"%s","text":"%s"}' \
                "$(esc "$(L 'Cancelled' 'Cancelado')")" "$(esc "$(L 'Nothing was changed.' 'Nada foi alterado.')")"
            exit 0
        fi
        break
    done
    ARG_YES="yes"   # a confirmação já foi o diálogo do Kai
}

# Checklist da execução: um item por etapa planejada.
kip_steps_begin() {
    exec </dev/null   # os filhos não podem ler (nem engolir) as mensagens do Kai
    local items="" id label
    add_item() { items+="${items:+,}{\"id\":\"$1\",\"label\":\"$(esc "$2")\"}"; }
    [[ "$DO_BUMP" == "1" ]] && add_item bump "$(L "Bump the version to ${VERSION}" "Atualizar a versão para ${VERSION}")"
    [[ "$BUILD_LINUX" == "1" ]] && add_item build_linux "$(L 'Build the Linux AppImage' 'Buildar o AppImage do Linux')"
    [[ "$BUILD_WINDOWS" == "1" ]] && add_item build_windows "$(L 'Build the Windows package' 'Buildar o pacote do Windows')"
    [[ "$BUILD_LINUX" == "1" || "$BUILD_WINDOWS" == "1" ]] && add_item package "$(L 'Package the artifacts' 'Empacotar os artefatos')"
    [[ "$DO_COMMIT" == "1" ]] && add_item commit "$(L 'Commit the bump' 'Commitar o bump')"
    [[ -n "$TAG" ]] && add_item tag "$(L "Create tag ${TAG}" "Criar a tag ${TAG}")"
    [[ "$DO_PUSH" == "1" ]] && add_item push "$(L 'Push to origin' 'Push para o origin')"
    [[ -n "$RELEASE_TAG" ]] && add_item release "$(L "GitHub Release ${RELEASE_TAG}" "Release ${RELEASE_TAG} no GitHub")"
    if [[ -z "$items" ]]; then
        send '{"kip":1,"type":"done","level":"info","title":"%s","text":"%s"}' \
            "$(esc "$(L 'Nothing to do' 'Nada a fazer')")" "$(esc "$(L 'The plan has no steps.' 'O plano não tem etapas.')")"
        exit 0
    fi
    send '{"kip":1,"type":"steps","id":"release","title":"%s","items":[%s]}' \
        "$(esc "$(L "Releasing ${VERSION}" "Release ${VERSION}")")" "$items"
    set -E
    trap 'kip_on_error' ERR
}

# kstep <etapa> <running|success|error> [detalhe]
kstep() {
    [[ "$KIP_READY" == "1" ]] || return 0
    [[ "$2" == "running" ]] && KIP_STEP="$1"
    [[ "$2" == "success" ]] && KIP_STEP=""
    if [[ -n "${3:-}" ]]; then
        send '{"kip":1,"type":"step","steps":"release","id":"%s","state":"%s","detail":"%s"}' "$1" "$2" "$(esc "$3")"
    else
        send '{"kip":1,"type":"step","steps":"release","id":"%s","state":"%s"}' "$1" "$2"
    fi
}

kip_on_error() {
    local rc=$?
    trap - ERR
    [[ -n "$KIP_STEP" ]] && kstep "$KIP_STEP" error "exit $rc"
    local tail_text note=""
    tail_text="$(tail -n 12 "$KIP_LOG" | sed 's/\x1b\[[0-9;?]*[A-Za-z]//g')"
    # O bump é feito em disco antes do build: se algo falha depois, ele continua lá (como no terminal).
    if [[ "$DO_BUMP" == "1" ]] && ! git diff --quiet -- "$CMAKE_FILE" "$NSI_FILE" 2>/dev/null; then
        note=$'\n\n'"$(L "The version bump (${VERSION}) is still applied in your working tree, not committed." "O bump de versão (${VERSION}) continua aplicado no seu working tree, sem commit.")"
    fi
    send '{"kip":1,"type":"done","level":"error","title":"%s","text":"%s"}' \
        "$(esc "$(L 'The release failed' 'A release falhou')")" \
        "$(esc "$(L "Step '${KIP_STEP:-?}' exited with code ${rc}. Last lines:" "A etapa '${KIP_STEP:-?}' saiu com código ${rc}. Últimas linhas:")"$'\n\n'"$tail_text${note}")"
    exit "$rc"
}

kip_finish() {
    local text actions
    text="$(L "Version ${VERSION} is ready." "A versão ${VERSION} está pronta.")"
    if [[ "${#ARTIFACTS[@]}" -gt 0 ]]; then
        text+=$'\n\n'"$(L 'Artifacts:' 'Artefatos:')"$'\n'"$(printf '  %s\n' "${ARTIFACTS[@]}")"
    fi
    [[ -n "$TAG" ]] && text+=$'\n'"Tag: ${TAG}"
    [[ -n "$RELEASE_URL" ]] && text+=$'\n'"Release: ${RELEASE_URL}"
    actions="{\"type\":\"copy\",\"label\":\"$(esc "$(L 'Copy the version' 'Copiar a versão')")\",\"value\":\"$(esc "$VERSION")\"}"
    [[ -d "$REPO_ROOT/dist" ]] && actions="{\"type\":\"reveal\",\"label\":\"$(esc "$(L 'Reveal dist/' 'Mostrar dist/')")\",\"path\":\"$(esc "$REPO_ROOT/dist")\",\"path_format\":\"posix\"},${actions}"
    [[ "$RELEASE_URL" == http* ]] && actions="{\"type\":\"open_url\",\"label\":\"$(esc "$(L 'Open the Release' 'Abrir a Release')")\",\"url\":\"$(esc "$RELEASE_URL")\"},${actions}"
    send '{"kip":1,"type":"done","title":"%s","text":"%s","actions":[%s]}' \
        "$(esc "$(L "Release ${VERSION} done" "Release ${VERSION} concluída")")" "$(esc "$text")" "$actions"
}

# --- respostas prévias (flags) ------------------------------------------------
# Vazio = perguntar. Flags com valor aceitam "--flag=valor" e "--flag valor".

ARG_BUMP=""      # none | patch | X.Y.Z
ARG_BUILD=""     # none | linux | windows | both
ARG_COMMIT=""    # yes | no
ARG_TAG=""       # none | auto | NOME
ARG_PUSH=""      # yes | no
ARG_RELEASE=""   # none | auto | TAG
ARG_DIRTY=""     # yes
ARG_YES=""       # yes
KIP=0            # 1 = modo KIP (--kip)
KIP_READY=0      # 1 = o canal do protocolo já foi aberto (kip_init)
DC_T=""          # "-T" no modo KIP: sem TTY o `docker compose run` precisa dele

usage() {
    sed -n '/^#  Respostas prévias/,/^#  Ex\./p' "$0" | sed 's/^#  \{0,1\}//'
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bump=*) ARG_BUMP="${1#*=}" ;;
        --bump) [[ $# -ge 2 ]] || die "--bump precisa de um valor (X.Y.Z ou patch)"; ARG_BUMP="$2"; shift ;;
        --no-bump) ARG_BUMP="none" ;;
        --build=*) ARG_BUILD="${1#*=}" ;;
        --build) [[ $# -ge 2 ]] || die "--build precisa de um valor (linux|windows|both|none)"; ARG_BUILD="$2"; shift ;;
        --commit) ARG_COMMIT="yes" ;;
        --commit=*) ARG_COMMIT="${1#*=}" ;;
        --no-commit) ARG_COMMIT="no" ;;
        --tag=*) ARG_TAG="${1#*=}" ;;
        --tag) ARG_TAG="auto" ;;
        --no-tag) ARG_TAG="none" ;;
        --push) ARG_PUSH="yes" ;;
        --push=*) ARG_PUSH="${1#*=}" ;;
        --no-push) ARG_PUSH="no" ;;
        --release=*) ARG_RELEASE="${1#*=}" ;;
        --release) ARG_RELEASE="auto" ;;
        --no-release) ARG_RELEASE="none" ;;
        --allow-dirty) ARG_DIRTY="yes" ;;
        -y|--yes) ARG_YES="yes" ;;
        --kip) KIP=1 ;;
        -h|--help) usage; exit 0 ;;
        *) die "Flag desconhecida: $1 (veja --help)" ;;
    esac
    shift
done
case "$ARG_BUILD" in ""|none|linux|windows|both) ;; *) die "--build inválido: $ARG_BUILD (linux|windows|both|none)" ;; esac
case "$ARG_COMMIT" in ""|yes|no) ;; *) die "--commit inválido: $ARG_COMMIT (yes|no)" ;; esac
case "$ARG_PUSH" in ""|yes|no) ;; *) die "--push inválido: $ARG_PUSH (yes|no)" ;; esac

# --- estado inicial ------------------------------------------------------------

command -v git >/dev/null || die "git não encontrado."
HAS_GH=0
command -v gh >/dev/null 2>&1 && HAS_GH=1

if [[ "$KIP" == "1" ]]; then
    kip_begin
fi

if [[ -n "$(git status --porcelain)" ]]; then
    warn "Há mudanças não commitadas no working tree:"
    git status --short
    [[ "$ARG_DIRTY" == "yes" ]] || ask_yes_no "Continuar mesmo assim?" n || exit 1
fi

CURRENT_VERSION="$(current_version)"
BRANCH="$(git rev-parse --abbrev-ref HEAD)"
log "Versão atual: $CURRENT_VERSION  |  branch: $BRANCH"
echo

# =============================================================================
# FASE 1 — perguntas (nada é alterado aqui)
# =============================================================================

# Calcula o plano a partir das respostas (ARG_*), perguntando o que faltar — no terminal — e
# validando. É uma função para o modo KIP poder rodá-la de novo a cada tentativa do formulário
# (e numa subshell, só para validar). Define VERSION, DO_BUMP, BUILD_*, DO_COMMIT, TAG,
# DO_PUSH e RELEASE_TAG.
resolve_plan() {
    # 1) Bump de versão
    DO_BUMP=0
    VERSION="$CURRENT_VERSION"
    if [[ "$ARG_BUMP" == "none" ]]; then
        :
    elif [[ -n "$ARG_BUMP" ]]; then
        VERSION="$ARG_BUMP"
        [[ "$VERSION" == "patch" ]] && VERSION="$(next_patch_version "$CURRENT_VERSION")"
        DO_BUMP=1
    elif ask_yes_no "Fazer bump de versão? (atual: $CURRENT_VERSION)" n; then
        VERSION="$(ask_value "Nova versão (semver X.Y.Z)" "$(next_patch_version "$CURRENT_VERSION")")"
        DO_BUMP=1
    fi
    [[ "$DO_BUMP" == "0" ]] || is_semver "$VERSION" || die "Versão inválida: $VERSION (esperado X.Y.Z)"

    # 2) Build de artefatos
    BUILD_LINUX=0
    BUILD_WINDOWS=0
    if [[ -z "$ARG_BUILD" ]]; then
        ARG_BUILD="none"
        if ask_yes_no "Buildar novos artefatos?" y; then
            echo "  [1] Linux (AppImage)"
            echo "  [2] Windows (kai.exe + instalador NSIS)"
            echo "  [3] Ambos"
            case "$(ask_value "Alvo do build" "3")" in
                1) ARG_BUILD="linux" ;;
                2) ARG_BUILD="windows" ;;
                3) ARG_BUILD="both" ;;
                *) die "Alvo inválido (esperado 1, 2 ou 3)" ;;
            esac
        fi
    fi
    [[ "$ARG_BUILD" == "linux" || "$ARG_BUILD" == "both" ]] && BUILD_LINUX=1
    [[ "$ARG_BUILD" == "windows" || "$ARG_BUILD" == "both" ]] && BUILD_WINDOWS=1

    # 3) Commit do bump
    DO_COMMIT=0
    if [[ "$DO_BUMP" == "1" ]]; then
        if [[ -n "$ARG_COMMIT" ]]; then
            [[ "$ARG_COMMIT" == "yes" ]] && DO_COMMIT=1
        elif ask_yes_no "Commitar o bump ($VERSION) na branch '$BRANCH'?" y; then
            DO_COMMIT=1
        fi
    fi

    # 4) Tag git
    TAG=""
    if [[ "$ARG_TAG" == "auto" ]]; then
        TAG="v${VERSION}"
    elif [[ -n "$ARG_TAG" && "$ARG_TAG" != "none" ]]; then
        TAG="$ARG_TAG"
    elif [[ -z "$ARG_TAG" ]] && ask_yes_no "Criar tag git para este release?" "$([[ $DO_BUMP == 1 ]] && echo y || echo n)"; then
        TAG="$(ask_value "Nome da tag" "v${VERSION}")"
    fi
    if [[ -n "$TAG" ]] && git rev-parse "$TAG" >/dev/null 2>&1; then
        die "Tag '$TAG' já existe localmente."
    fi

    # 5) Push
    DO_PUSH=0
    if [[ -n "$TAG" || "$DO_BUMP" == "1" ]]; then
        if [[ -n "$ARG_PUSH" ]]; then
            [[ "$ARG_PUSH" == "yes" ]] && DO_PUSH=1
        elif ask_yes_no "Dar push de '$BRANCH'${TAG:+ e da tag '$TAG'} para origin?" n; then
            DO_PUSH=1
        fi
    fi

    # 6) GitHub Release — a tag criada acima ou, sem tag nova (ex: só rebuild),
    #    uma existente.
    RELEASE_TAG=""
    if [[ "$ARG_RELEASE" == "none" ]]; then
        :
    elif [[ "$HAS_GH" == "0" ]]; then
        [[ -n "$ARG_RELEASE" ]] && die "--release pedido, mas o gh CLI não foi encontrado."
        warn "gh CLI não encontrado — a publicação da Release no GitHub fica de fora."
    elif [[ "$ARG_RELEASE" == "auto" ]]; then
        RELEASE_TAG="${TAG:-v${VERSION}}"
    elif [[ -n "$ARG_RELEASE" ]]; then
        RELEASE_TAG="$ARG_RELEASE"
    elif [[ -n "$TAG" ]]; then
        ask_yes_no "Publicar/atualizar Release no GitHub para '$TAG'?" n && RELEASE_TAG="$TAG"
    elif ask_yes_no "Atualizar/criar Release no GitHub para a versão ${VERSION}?" n; then
        RELEASE_TAG="$(ask_value "Tag da Release" "v${VERSION}")"
    fi
    # Tag da Release que NÃO é a criada agora: precisa já existir (local ou origin).
    if [[ -n "$RELEASE_TAG" && "$RELEASE_TAG" != "$TAG" ]] \
        && ! git rev-parse "$RELEASE_TAG" >/dev/null 2>&1 \
        && ! git ls-remote --tags origin | grep -q "refs/tags/${RELEASE_TAG}$"; then
        die "Tag '$RELEASE_TAG' não existe local nem remotamente."
    fi
    return 0
}

if [[ "$KIP" == "1" ]]; then
    kip_collect   # formulário -> ARG_* -> resolve_plan -> plano -> confirmação do Kai
else
    resolve_plan
fi

# =============================================================================
# FASE 2 — plano + confirmação
# =============================================================================
yes_no() { [[ "$1" == "1" ]] && echo "sim" || echo "não"; }
print_plan() {
echo
log "Plano:"
if [[ "$DO_BUMP" == "1" ]]; then
    echo "   bump de versão ... $CURRENT_VERSION -> $VERSION"
else
    echo "   bump de versão ... não (fica $VERSION)"
fi
BUILD_DESC="não"
[[ "$BUILD_LINUX" == "1" && "$BUILD_WINDOWS" == "1" ]] && BUILD_DESC="Linux + Windows"
[[ "$BUILD_LINUX" == "1" && "$BUILD_WINDOWS" == "0" ]] && BUILD_DESC="Linux"
[[ "$BUILD_LINUX" == "0" && "$BUILD_WINDOWS" == "1" ]] && BUILD_DESC="Windows"
echo "   build ............ $BUILD_DESC"
[[ "$DO_BUMP" == "1" ]] && echo "   commit do bump ... $(yes_no "$DO_COMMIT")"
echo "   tag .............. ${TAG:-não}"
[[ -n "$TAG" || "$DO_BUMP" == "1" ]] && echo "   push ............. $(yes_no "$DO_PUSH")"
echo "   Release GitHub ... ${RELEASE_TAG:-não}"
echo
}
print_plan
if [[ "$ARG_YES" != "yes" ]]; then
    ask_yes_no "Confirmar e executar?" y || { log "Cancelado — nada foi alterado."; exit 0; }
fi

# =============================================================================
# FASE 3 — execução (sem perguntas)
# =============================================================================

[[ "$KIP" == "1" ]] && kip_steps_begin

if [[ "$DO_BUMP" == "1" ]]; then
    kstep bump running
    log "Atualizando $CMAKE_FILE e $NSI_FILE para $VERSION..."
    sed -i "s/project(kai VERSION [0-9]\+\.[0-9]\+\.[0-9]\+/project(kai VERSION ${VERSION}/" "$CMAKE_FILE"
    sed -i "s/!define APPVERSION \"[0-9]\+\.[0-9]\+\.[0-9]\+\"/!define APPVERSION \"${VERSION}\"/" "$NSI_FILE"
    sed -i "s/VIProductVersion \"[0-9]\+\.[0-9]\+\.[0-9]\+\.0\"/VIProductVersion \"${VERSION}.0\"/" "$NSI_FILE"
    sed -i "s/\"ProductVersion\"  *\"[0-9]\+\.[0-9]\+\.[0-9]\+\.0\"/\"ProductVersion\"  \"${VERSION}.0\"/" "$NSI_FILE"
    sed -i "s/\"FileVersion\"     *\"[0-9]\+\.[0-9]\+\.[0-9]\+\.0\"/\"FileVersion\"     \"${VERSION}.0\"/" "$NSI_FILE"
    log "Versão em disco agora: $(current_version)"
    git diff -- "$CMAKE_FILE" "$NSI_FILE"
    kstep bump success "$VERSION"
fi

if [[ "$BUILD_LINUX" == "1" ]]; then
    kstep build_linux running
    log "Buildando imagem Linux..."
    run_cmd docker compose build build-linux
    log "Gerando artefato Linux..."
    run_cmd docker compose run --rm $DC_T build-linux
    kstep build_linux success
fi
if [[ "$BUILD_WINDOWS" == "1" ]]; then
    kstep build_windows running
    log "Buildando imagem Windows (cross-compile, pode demorar)..."
    run_cmd docker compose build build-windows
    log "Gerando artefato Windows + instalador..."
    run_cmd env KAI_BUILD_INSTALLER=1 docker compose run --rm $DC_T build-windows
    kstep build_windows success
fi
[[ "$BUILD_LINUX" == "0" && "$BUILD_WINDOWS" == "0" ]] \
    && log "Sem build — assumindo que dist/ já tem os artefatos certos para $VERSION."

# --- renomeia/empacota os artefatos com a versão alvo -------------------------

LINUX_ARTIFACT="dist/kai-${VERSION}-linux-x86_64.AppImage"
WINDOWS_SETUP="dist/kai-${VERSION}-setup.exe"
WINDOWS_ZIP="dist/kai-${VERSION}-windows-x86_64.zip"

[[ "$BUILD_LINUX" == "1" || "$BUILD_WINDOWS" == "1" ]] && kstep package running
if [[ "$BUILD_LINUX" == "1" ]]; then
    [[ -f dist/Kai-x86_64.AppImage ]] || die "dist/Kai-x86_64.AppImage não encontrado após o build."
    cp -f dist/Kai-x86_64.AppImage "$LINUX_ARTIFACT"
    log "Artefato Linux: $LINUX_ARTIFACT"
fi

if [[ "$BUILD_WINDOWS" == "1" ]]; then
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
[[ "$BUILD_LINUX" == "1" || "$BUILD_WINDOWS" == "1" ]] && kstep package success "${#ARTIFACTS[@]} $([[ "$KIP_READY" == "1" ]] && L 'file(s)' 'arquivo(s)')"

if [[ "${#ARTIFACTS[@]}" -gt 0 ]]; then
    log "Artefatos prontos para $VERSION:"
    printf '   %s\n' "${ARTIFACTS[@]}"
fi

if [[ "$DO_COMMIT" == "1" ]]; then
    kstep commit running
    git add "$CMAKE_FILE" "$NSI_FILE"
    git commit -m "chore(release): bump version to ${VERSION}"
    log "Commit criado: $(git rev-parse --short HEAD)"
    kstep commit success "$(git rev-parse --short HEAD)"
elif [[ "$DO_BUMP" == "1" ]]; then
    warn "Bump feito em disco mas NÃO commitado — o working tree ficou sujo."
fi

if [[ -n "$TAG" ]]; then
    kstep tag running
    git tag -a "$TAG" -m "Kai ${VERSION}"
    log "Tag criada: $TAG (em $(git rev-parse --short HEAD))"
    kstep tag success "$TAG"
fi

if [[ "$DO_PUSH" == "1" ]]; then
    kstep push running
    git push origin "$BRANCH"
    [[ -n "$TAG" ]] && git push origin "$TAG"
    log "Push concluído."
    kstep push success
elif [[ -n "$TAG" || "$DO_BUMP" == "1" ]]; then
    warn "Nada enviado ao origin — branch/tag continuam só locais."
fi

if [[ -n "$RELEASE_TAG" ]]; then
    kstep release running
    if ! git ls-remote --tags origin | grep -q "refs/tags/${RELEASE_TAG}$"; then
        warn "Tag '$RELEASE_TAG' ainda não está no origin — dando push dela agora."
        git push origin "$RELEASE_TAG"
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
    RELEASE_URL="$(gh release view "$RELEASE_TAG" --json url -q .url)"
    log "Release: $RELEASE_URL"
    kstep release success "$RELEASE_TAG"
fi

log "Concluído."
[[ "$KIP" == "1" ]] && kip_finish
exit 0

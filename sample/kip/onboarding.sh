#!/usr/bin/env bash
# KIP demo 5/8 — Team onboarding: a checklist whose items change state live (kip_window: true in kai.json).
# Plain bash: KIP is JSON Lines — we print one JSON object per line on stdout and
# read Kai's answers (one JSON line each) from stdin. No helper, no jq, no `kai`.
set -u

# Prints one line of JSON. printf-style: send '{"a":"%s"}' "$(esc "$value")".
# Newlines/indentation of the format are dropped, so long messages stay readable.
send() { printf "$1" "${@:2}" | tr -d '\n'; echo; }
# Escapes a value for use inside a JSON string.
esc() { local s=${1//\\/\\\\}; s=${s//\"/\\\"}; s=${s//$'\n'/\\n}; printf '%s' "${s//$'\t'/\\t}"; }
# First value of "key" in $MSG: a string (unescaped), number, true/false or null.
jget() {
    local v
    v=$(printf '%s' "$MSG" | sed -nE 's/.*"'"$1"'":"(([^"\\]|\\.)*)".*/\1/p')
    [ -z "$v" ] && v=$(printf '%s' "$MSG" | sed -nE 's/.*"'"$1"'":(true|false|null|-?[0-9.]+).*/\1/p')
    v=${v//\\\\/$'\001'}; v=${v//\\\"/\"}; v=${v//\\\//\/}; v=${v//\\n/$'\n'}; v=${v//$'\001'/\\}
    printf '%s' "$v"
}
# Waits for Kai's next message into $MSG. A "cancel" means the user gave up.
recv() { IFS= read -r MSG || exit 130; [ "$(jget type)" = cancel ] && exit 130; return 0; }
send '{"kip":1,"type":"hello","title":"Team onboarding","version":"0.9"}'
send '{"kip":1,"type":"markdown","text":"## Welcome aboard\nThis checklist sets up your machine. It runs on its own — **watch the steps tick off**."}'
send '{"kip":1,"type":"steps","id":"setup","title":"Setting up your machine",
    "items":[{"id":"toolchain","label":"Install the toolchain"},{"id":"git","label":"Configure git"},
             {"id":"repos","label":"Clone the repositories"},{"id":"tests","label":"Run the smoke tests"},
             {"id":"chat","label":"Join the chat server"},{"id":"vpn","label":"Request VPN access"}]}'

# step <item> <state> [detail]
step() {
    if [ -n "${3:-}" ]; then
        send '{"kip":1,"type":"step","steps":"setup","id":"%s","state":"%s","detail":"%s"}' "$1" "$2" "$(esc "$3")"
    else
        send '{"kip":1,"type":"step","steps":"setup","id":"%s","state":"%s"}' "$1" "$2"
    fi
}

step toolchain running "gcc, cmake, ninja"; sleep 0.8; step toolchain success "gcc 13.2 · cmake 3.28"
step git running "name and email";          sleep 0.5; step git success
step repos running "cloning 3 of 7";        sleep 1.0; step repos success "7 repositories"
step tests running "running…";              sleep 0.8; step tests success "42 passed"
step chat running;                          sleep 0.4; step chat skipped "already a member"
step vpn running "contacting the ticket system"; sleep 0.8; step vpn error "ticket system unreachable"

send '{"kip":1,"type":"message","level":"warning","text":"The VPN request failed; retry it later from the wiki."}'
send '{"kip":1,"type":"notify","title":"Onboarding finished","text":"5 of 6 steps done","level":"warning"}'
send '{"kip":1,"type":"done","level":"warning","title":"Almost done",
    "text":"Everything is set up except the VPN access.",
    "actions":[{"type":"open_url","label":"Open the wiki","url":"https://example.com/wiki/vpn"}]}'

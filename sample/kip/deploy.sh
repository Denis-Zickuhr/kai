#!/usr/bin/env bash
# KIP demo 1/8 — Deploy wizard: environment + flags -> danger confirm (Back) -> progress -> done with actions.
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
send '{"kip":1,"type":"hello","title":"Deploy","version":"1.4.2"}'

while true; do
    send '{"kip":1,"type":"prompt","id":"target","title":"Where to deploy?",
        "description":"Pick the environment and a few options. Your last choices are remembered.",
        "submit_label":"Next",
        "fields":[
            {"name":"env","type":"select","label":"Environment","required":true,"default":"staging",
             "options":[{"value":"dev","label":"Development"},
                        {"value":"staging","label":"Staging","description":"Mirror of production"},
                        {"value":"prod","label":"Production","description":"Customer facing"}]},
            {"name":"opts","type":"flags","label":"Options",
             "options":[{"name":"dry","label":"Dry run","default":false},
                        {"name":"notify","label":"Notify the team","default":true}]}
        ]}'
    recv
    env=$(jget env)
    dry=$(jget dry)

    if [ "$env" = prod ]; then
        send '{"kip":1,"type":"confirm","id":"sure","title":"Deploy to production?","danger":true,
            "text":"Version 1.4.2 goes live for every customer. This cannot be undone from here.",
            "confirm_label":"Deploy now","cancel_label":"Not now","back":true}'
        recv
        [ "$(jget type)" = back ] && continue
        if [ "$(jget confirmed)" != true ]; then
            send '{"kip":1,"type":"done","level":"warning","title":"Deploy cancelled","text":"Nothing was deployed."}'
            exit 0
        fi
    fi
    break
done

suffix=''; [ "$dry" = true ] && suffix=' (dry run)'
send '{"kip":1,"type":"message","level":"info","text":"Deploying version 1.4.2 to %s%s…"}' "$(esc "$env")" "$suffix"
for pct in 10 25 40 60 80 100; do
    send '{"kip":1,"type":"progress","value":%s,"label":"Uploading artifacts… %s%%"}' "$pct" "$pct"
    sleep 0.5
done
send '{"kip":1,"type":"done","title":"Deployed to %s","text":"Version 1.4.2 is live%s.",
    "actions":[{"type":"open_url","label":"Open the site","url":"https://example.com"},
               {"type":"copy","label":"Copy release id","value":"rel-%s"}]}' \
    "$(esc "$env")" "$suffix" "$(date +%s)"

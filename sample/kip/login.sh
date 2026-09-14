#!/usr/bin/env bash
# KIP demo 6/8 — Login: a secret field, then set_env exports KAI_DEMO_TOKEN (declared in the command's "Exportable variables"). Demo password: kai
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
send '{"kip":1,"type":"hello","title":"Sign in"}'

send '{"kip":1,"type":"prompt","id":"login","title":"Sign in to Acme","submit_label":"Sign in",
    "description":"Demo credentials: any user, password '"'"'kai'"'"'.",
    "fields":[
        {"name":"user","type":"text","label":"User","required":true,"default":"%s"},
        {"name":"password","type":"secret","label":"Password","required":true}
    ]}' "$(esc "${USER:-demo}")"

# "invalid" keeps the same prompt open: just wait for the next answer.
attempts=0
while true; do
    recv
    user=$(jget user)
    [ "$(jget password)" = kai ] && break
    attempts=$((attempts + 1))
    if [ "$attempts" -ge 3 ]; then
        send '{"kip":1,"type":"done","level":"error","title":"Too many attempts","text":"Try again in a few minutes."}'
        exit 1
    fi
    send '{"kip":1,"type":"invalid","id":"login","message":"Wrong user or password (%s of 3)",
        "errors":{"password":"Check the password and try again"}}' "$attempts"
done

send '{"kip":1,"type":"progress","value":null,"label":"Talking to the identity provider…"}'
sleep 1
token="demo-$(printf '%s' "$user:$(date +%s)" | base64 | tr -d '=\n' | cut -c1-24)"
# Only names listed in the command's "Exportable variables" are accepted by Kai.
send '{"kip":1,"type":"set_env","name":"KAI_DEMO_TOKEN","value":"%s"}' "$(esc "$token")"
send '{"kip":1,"type":"done","title":"Signed in as %s",
    "text":"The token was exported to KAI_DEMO_TOKEN; run the '"'"'Show token'"'"' command next.",
    "actions":[{"type":"copy","label":"Copy token","value":"%s"}]}' "$(esc "$user")" "$(esc "$token")"

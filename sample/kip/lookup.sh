#!/usr/bin/env bash
# KIP demo 8/8 — Lookup: query -> result table -> copy action.
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
customers='c-101|Acme Corp|enterprise|12000
c-102|Acme Labs|team|900
c-103|Acme Foods|free|0
c-201|Globex|enterprise|18000
c-202|Globex Retail|team|1200
c-301|Initech|team|800'

send '{"kip":1,"type":"hello","title":"Customer lookup"}'
send '{"kip":1,"type":"prompt","id":"q","title":"Find a customer","submit_label":"Search",
    "fields":[{"name":"query","type":"text","label":"Name contains","required":true,"default":"acme","placeholder":"acme"}]}'

# "invalid" keeps the same prompt open: just wait for the next answer.
while true; do
    recv
    q=$(jget query)
    matches=$(printf '%s\n' "$customers" | grep -i -- "|[^|]*$q[^|]*|" || true)
    [ -n "$matches" ] && break
    send '{"kip":1,"type":"invalid","id":"q","message":"Nothing found","errors":{"query":"No customer matches '"'"'%s'"'"'"}}' "$(esc "$q")"
done

rows=''; first_id=''
while IFS='|' read -r id name plan mrr; do
    [ -z "$first_id" ] && first_id="$id"
    rows+="${rows:+,}{\"id\":\"$id\",\"name\":\"$(esc "$name")\",\"plan\":\"$plan\",\"mrr\":$mrr}"
done <<< "$matches"
send '{"kip":1,"type":"table","id":"results","title":"Customers matching '"'"'%s'"'"'",
    "columns":[{"key":"id","label":"ID"},{"key":"name","label":"Name"},{"key":"plan","label":"Plan"},{"key":"mrr","label":"MRR"}],
    "rows":[%s]}' "$(esc "$q")" "$rows"
send '{"kip":1,"type":"done","title":"%s customer(s) found",
    "text":"Select a cell in the table and press Ctrl+C to copy it.",
    "actions":[{"type":"copy","label":"Copy first id","value":"%s"}]}' \
    "$(printf '%s\n' "$matches" | wc -l | tr -d ' ')" "$(esc "$first_id")"

#!/usr/bin/env bash
# KIP demo 3/8 — Cascading navigation (context -> namespace -> pod): watch + patch.
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
namespaces_for() {
    case "$1" in
        prod-eu) echo "default,payments,checkout" ;;
        prod-us) echo "default,billing" ;;
        staging) echo "default,sandbox" ;;
    esac
}
pods_for() {
    case "$1/$2" in
        prod-eu/default)  echo "api-7d9f,api-8e1c,worker-1a2b" ;;
        prod-eu/payments) echo "ledger-0,ledger-1,gateway-5c4d" ;;
        prod-eu/checkout) echo "cart-6b1a,cart-9f3e" ;;
        prod-us/default)  echo "api-2a2a,api-3b3b" ;;
        prod-us/billing)  echo "invoicer-0" ;;
        staging/default)  echo "api-dev-1" ;;
        staging/sandbox)  echo "playground-0,playground-1" ;;
    esac
}
first_of() { echo "${1%%,*}"; }
# "a,b,c" -> ["a","b","c"]
arr() { local IFS=, out="" x; for x in $1; do out+="${out:+,}\"$(esc "$x")\""; done; printf '[%s]' "$out"; }

send '{"kip":1,"type":"hello","title":"Cluster navigator"}'

ctx=prod-eu
ns=$(first_of "$(namespaces_for "$ctx")")
send '{"kip":1,"type":"prompt","id":"nav","title":"Pick a pod","submit_label":"Open logs",
    "description":"Change the context and the namespace and pod lists follow.",
    "fields":[
        {"name":"context","type":"select","label":"Context","required":true,"watch":true,"default":"%s","options":%s},
        {"name":"namespace","type":"select","label":"Namespace","required":true,"watch":true,"default":"%s","options":%s},
        {"name":"pod","type":"list","label":"Pod","required":true,"options":%s}
    ]}' "$ctx" "$(arr prod-eu,prod-us,staging)" "$ns" "$(arr "$(namespaces_for "$ctx")")" "$(arr "$(pods_for "$ctx" "$ns")")"

while true; do
    recv
    case "$(jget type)" in
        change)
            # Every "change" MUST be answered with a "patch" that echoes its seq.
            seq=$(jget seq)
            ctx=$(jget context)
            ns=$(jget namespace)
            if [ "$(jget field)" = context ]; then
                # a different context: its first namespace becomes the default
                ns=$(first_of "$(namespaces_for "$ctx")")
                send '{"kip":1,"type":"patch","id":"nav","seq":%s,"fields":[
                    {"name":"namespace","type":"select","label":"Namespace","required":true,"watch":true,"default":"%s","options":%s},
                    {"name":"pod","type":"list","label":"Pod","required":true,"options":%s}]}' \
                    "$seq" "$ns" "$(arr "$(namespaces_for "$ctx")")" "$(arr "$(pods_for "$ctx" "$ns")")"
            else
                send '{"kip":1,"type":"patch","id":"nav","seq":%s,"fields":[
                    {"name":"pod","type":"list","label":"Pod","required":true,"options":%s}]}' \
                    "$seq" "$(arr "$(pods_for "$ctx" "$ns")")"
            fi
            ;;
        response)
            pod=$(jget pod)
            send '{"kip":1,"type":"done","title":"Selected %s","text":"Context %s / namespace %s.",
                "actions":[{"type":"copy","label":"Copy kubectl command","value":"kubectl --context %s -n %s logs %s"}]}' \
                "$(esc "$pod")" "$(esc "$ctx")" "$(esc "$ns")" "$(esc "$ctx")" "$(esc "$ns")" "$(esc "$pod")"
            exit 0
            ;;
    esac
done

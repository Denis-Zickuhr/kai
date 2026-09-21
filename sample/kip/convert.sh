#!/usr/bin/env bash
# KIP demo 7/8 — File conversion: file picker + options -> progress -> done with "Reveal output".
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
send '{"kip":1,"type":"hello","title":"Converter"}'

send '{"kip":1,"type":"prompt","id":"convert","title":"What do you want to convert?","submit_label":"Convert",
    "fields":[
        {"name":"input","type":"filepick","label":"Input file","required":true,
         "filter":"Media (*.png *.jpg *.jpeg *.mp4 *.mov);;All files (*)","path_format":"posix"},
        {"name":"format","type":"select","label":"Output format","required":true,"default":"webp",
         "options":[{"value":"webp","label":"WebP"},{"value":"jpg","label":"JPEG"},{"value":"png","label":"PNG"}]},
        {"name":"quality","type":"number","label":"Quality","min":1,"max":100,"default":80,
         "description":"Higher is better and bigger."},
        {"name":"opts","type":"flags","label":"Options",
         "options":[{"name":"strip","label":"Strip metadata","default":true},
                    {"name":"overwrite","label":"Overwrite if it exists","default":false}]}
    ]}'
# "invalid" keeps the same prompt open: just wait for the next answer.
while true; do
    recv
    input=$(jget input)
    format=$(jget format)
    quality=$(jget quality)
    # The result goes next to the input file, so it is where the user can see it.
    output="${input%.*}.$format"
    [ "$output" = "$input" ] && output="${input%.*}-converted.$format"
    if [ ! -f "$input" ]; then
        send '{"kip":1,"type":"invalid","id":"convert","errors":{"input":"File not found: %s"}}' "$(esc "$input")"
        continue
    fi
    if [ -e "$output" ] && [ "$(jget overwrite)" != true ]; then
        send '{"kip":1,"type":"invalid","id":"convert","errors":{"input":"%s already exists: tick the Overwrite option"}}' \
            "$(esc "$(basename "$output")")"
        continue
    fi
    break
done

for pct in 0 15 35 55 75 100; do
    send '{"kip":1,"type":"progress","value":%s,"label":"Converting %s to %s (quality %s)…"}' \
        "$pct" "$(esc "$(basename "$input")")" "$(esc "$format")" "$(esc "$quality")"
    sleep 0.4
done
# The demo does not really convert anything: it copies the file under the new extension.
if ! err=$(cp "$input" "$output" 2>&1); then
    send '{"kip":1,"type":"done","level":"error","title":"Could not write the output","text":"%s"}' "$(esc "$err")"
    exit 1
fi
send '{"kip":1,"type":"done","title":"Converted (demo: the file was copied, not re-encoded)","text":"%s",
    "actions":[{"type":"reveal","label":"Reveal output","path":"%s","path_format":"posix"},
               {"type":"copy","label":"Copy path","value":"%s"}]}' \
    "$(esc "$output")" "$(esc "$output")" "$(esc "$output")"

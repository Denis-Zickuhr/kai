#!/usr/bin/env bash
# KIP demo 4/8 — Scaffolding: text + list + folder + flags; "invalid" round trip; done with "Reveal folder".
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
send '{"kip":1,"type":"hello","title":"New project"}'

dest_default="/tmp/kai-kip-demo"
mkdir -p "$dest_default"
send '{"kip":1,"type":"prompt","id":"project","title":"Create a project","submit_label":"Create",
    "fields":[
        {"name":"name","type":"text","label":"Project name","required":true,"placeholder":"my-project",
         "description":"Lowercase letters, digits and dashes."},
        {"name":"template","type":"list","label":"Template","required":true,"default":"node-api",
         "options":[{"value":"node-api","label":"Node API","description":"Express + TypeScript"},
                    {"value":"python-cli","label":"Python CLI","description":"argparse + pytest"},
                    {"value":"static-site","label":"Static site","description":"Plain HTML + CSS"},
                    {"value":"rust-lib","label":"Rust library","description":"cargo workspace"}]},
        {"name":"dest","type":"folderpick","label":"Destination folder","required":true,"default":"%s",
         "path_format":"posix"},
        {"name":"opts","type":"flags","label":"Extras",
         "options":[{"name":"git","label":"Initialise a git repository","default":true},
                    {"name":"readme","label":"Add a README","default":true},
                    {"name":"license","label":"Add a LICENSE","default":false}]}
    ]}' "$(esc "$dest_default")"

# "invalid" keeps the SAME prompt open with the errors shown: just wait for the
# next answer (sending the prompt again would replace the screen and lose them).
while true; do
    recv
    name=$(jget name)
    if ! [[ "$name" =~ ^[a-z][a-z0-9-]*$ ]]; then
        send '{"kip":1,"type":"invalid","id":"project","message":"That name can'"'"'t be used",
            "errors":{"name":"Start with a letter; then lowercase letters, digits or dashes only"}}'
        continue
    fi
    break
done

template=$(jget template)
dest=$(jget dest)
target="$dest/$name"
send '{"kip":1,"type":"message","level":"info","text":"Creating %s from the %s template…"}' "$(esc "$target")" "$(esc "$template")"
# Nothing here is silent: if the folder can't be created, say so instead of "success".
if ! mkdir -p "$target/src" 2>/tmp/kai-kip-scaffold.err; then
    send '{"kip":1,"type":"done","level":"error","title":"Could not create the project",
        "text":"%s"}' "$(esc "$(cat /tmp/kai-kip-scaffold.err)")"
    exit 1
fi
[ "$(jget readme)" = true ] && echo "# $name" > "$target/README.md"
[ "$(jget license)" = true ] && echo "MIT License" > "$target/LICENSE"
[ "$(jget git)" = true ] && git init -q "$target" 2>/dev/null
echo "// $template entry point" > "$target/src/main"
sleep 0.5
files=$(cd "$target" && find . -path ./.git -prune -o -type f -print | sort | sed 's|^\./|  |')
# path_format "posix": a Windows-side Kai maps /mnt/c/... to C:\... and, for paths that only
# exist inside WSL, to \\wsl.localhost\<distro>\... (distro taken from the terminal target).
send '{"kip":1,"type":"done","title":"Project created","text":"%s",
    "actions":[{"type":"reveal","label":"Reveal folder","path":"%s","path_format":"posix"},
               {"type":"copy","label":"Copy path","value":"%s"}]}' \
    "$(esc "$target contains:"$'\n'"$files")" \
    "$(esc "$target")" "$(esc "$target")"

#!/usr/bin/env bash
# KIP demo 2/8 — Database restore: pick a backup in a TABLE field -> destructive confirm -> steps checklist.
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
send '{"kip":1,"type":"hello","title":"Database restore"}'

send '{"kip":1,"type":"prompt","id":"backup","title":"Which backup?","submit_label":"Restore…",
    "fields":[
        {"name":"file","type":"table","label":"Available backups","required":true,"default":"b2","row_key":"id",
         "columns":[{"key":"name","label":"Backup"},{"key":"size","label":"Size"},{"key":"taken","label":"Taken at"}],
         "rows":[{"id":"b1","name":"app-2024-03-01.sql.gz","size":"1.1 GB","taken":"2024-03-01 02:00"},
                 {"id":"b2","name":"app-2024-03-02.sql.gz","size":"1.2 GB","taken":"2024-03-02 02:00"},
                 {"id":"b3","name":"app-2024-03-03.sql.gz","size":"1.2 GB","taken":"2024-03-03 02:00"},
                 {"id":"b4","name":"app-2024-03-04.sql.gz","size":"1.3 GB","taken":"2024-03-04 02:00"}]}
    ]}'
recv
id=$(jget file)

send '{"kip":1,"type":"confirm","id":"sure","title":"Overwrite the database?","danger":true,"back":true,
    "text":"This replaces the CURRENT database with backup %s. Anything newer than the backup is lost.",
    "confirm_label":"Restore","cancel_label":"Keep current data"}' "$(esc "$id")"
recv
if [ "$(jget confirmed)" != true ]; then
    send '{"kip":1,"type":"done","level":"info","title":"Nothing changed","text":"The current database was kept."}'
    exit 0
fi

send '{"kip":1,"type":"steps","id":"restore","title":"Restoring %s",
    "items":[{"id":"stop","label":"Stop the application"},{"id":"snapshot","label":"Take a safety snapshot"},
             {"id":"load","label":"Load the backup"},{"id":"migrate","label":"Run migrations"},
             {"id":"start","label":"Start the application"}]}' "$(esc "$id")"
for s in stop snapshot load migrate start; do
    send '{"kip":1,"type":"step","steps":"restore","id":"%s","state":"running"}' "$s"
    sleep 0.7
    send '{"kip":1,"type":"step","steps":"restore","id":"%s","state":"success"}' "$s"
done
send '{"kip":1,"type":"done","title":"Database restored","text":"Backup %s is now live.",
    "actions":[{"type":"copy","label":"Copy backup id","value":"%s"}]}' "$(esc "$id")" "$(esc "$id")"

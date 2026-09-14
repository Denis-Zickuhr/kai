#!/usr/bin/env bash
# KIP demo 12 — Branch picker (quiver-style): a paged, filterable list plus CHIPS — small buttons that run an
# ephemeral action (context, sync, PR, delete) and show its result in a box, without leaving the step.
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

# Fake data: nothing here touches git. Branch names containing "conflict" make `sync` fail on purpose.
branches=(main feat/login feat/payments fix/cart-total fix/timezone chore/deps chore/ci refactor/api
          docs/readme feat/search fix/login-redirect perf/images)
current=feat/login

# The branches as KIP options: [{"value","label","description"}, ...]
options_json() {
    local out="" i b d
    for i in "${!branches[@]}"; do
        b=${branches[$i]}
        d="#$i"
        [ "$b" = main ] && d="default branch"
        [ "$b" = "$current" ] && d="$d · current"
        out+="${out:+,}{\"value\":\"$(esc "$b")\",\"label\":\"$(esc "$b")\",\"description\":\"$(esc "$d")\"}"
    done
    printf '[%s]' "$out"
}

# chip_result <chip> <running|success|error> <title> <markdown>
chip_result() {
    send '{"kip":1,"type":"chip_result","chip":"%s","state":"%s","title":"%s","text":"%s"}' \
        "$1" "$2" "$(esc "$3")" "$(esc "$4")"
}

send '{"kip":1,"type":"hello","title":"git quiver (demo)","version":"0.1"}'

# `page_size` pages the list, `searchable` forces the filter box. Chips are buttons under the
# fields: `requires` disables them until a branch is picked, `confirm` asks inside the box first.
send '{"kip":1,"type":"prompt","id":"pick","title":"Pick a branch","submit_label":"Switch",
    "description":"Demo data only: nothing touches git. Try the chips below the list.",
    "fields":[
        {"name":"branch","type":"list","label":"Branch","required":true,"default":"%s",
         "searchable":true,"page_size":5,"options":%s}
    ],
    "chips":[
        {"id":"ctx","label":"Context","icon":"info","description":"Ticket and pull request status","requires":["branch"]},
        {"id":"sync","label":"Sync","icon":"refresh-cw","description":"Rebase onto origin/main","requires":["branch"]},
        {"id":"pr","label":"Pull request","icon":"git-pull-request","requires":["branch"],
         "confirm":{"text":"Open a pull request for the selected branch?","confirm_label":"Open PR"}},
        {"id":"delete","label":"Delete branch","icon":"trash-2","danger":true,"requires":["branch"],
         "confirm":{"title":"Delete this branch?","text":"The local branch is removed. This cannot be undone.",
                    "confirm_label":"Delete","cancel_label":"Keep"}}
    ]}' "$(esc "$current")" "$(options_json)"

while true; do
    recv
    case "$(jget type)" in
    chip)
        # A chip click arrives with the CURRENT field values, like a `change`. The prompt stays
        # open; the answer is a series of chip_result messages: running... then success | error.
        chip=$(jget chip)
        b=$(jget branch)
        case "$chip" in
        ctx)
            chip_result ctx running "Context" "Looking up the ticket and the pull request…"
            sleep 0.8
            chip_result ctx success "Context · $b" "**Ticket** [ABC-${#b}42](https://example.com/browse/ABC-${#b}42)

- PR #$((RANDOM % 900 + 100)): *open*, 2 approvals
- CI: passing

\`\`\`
branch  $b
ahead   3
behind  0
\`\`\`"
            ;;
        sync)
            chip_result sync running "Sync" "Fetching origin…"
            sleep 0.6
            chip_result sync running "Sync" "Rebasing onto origin/main…"
            sleep 0.8
            if [ "$b" = fix/cart-total ]; then
                chip_result sync error "Sync failed" "\`\`\`
CONFLICT (content): Merge conflict in src/cart.cpp
error: could not apply 3f2a9c1... fix cart total
rebase aborted, your branch is unchanged
\`\`\`"
            else
                chip_result sync success "Synced · $b" "Rebased **2 commits** onto origin/main. Nothing to push."
            fi
            ;;
        pr)
            chip_result pr running "Pull request" "Pushing $b and opening the pull request…"
            sleep 1
            if [ "$b" = main ]; then
                chip_result pr error "No pull request" "\`main\` is the default branch: there is nothing to compare it with."
            else
                chip_result pr success "Pull request opened" "[#$((RANDOM % 900 + 100)) $b](https://example.com/pull/1)"
            fi
            ;;
        delete)
            chip_result delete running "Delete branch" "Deleting $b…"
            sleep 0.6
            if [ "$b" = main ] || [ "$b" = "$current" ]; then
                chip_result delete error "Not deleted" "Refusing to delete \`$b\`: it is the default or the current branch."
            else
                kept=()
                for x in "${branches[@]}"; do [ "$x" = "$b" ] || kept+=("$x"); done
                branches=("${kept[@]}")
                chip_result delete success "Deleted" "\`$b\` is gone."
                # The list shrinks: a patch (seq 0: it does not answer a "change") replaces the field.
                send '{"kip":1,"type":"patch","id":"pick","seq":0,"fields":[
                    {"name":"branch","type":"list","label":"Branch","required":true,"default":"%s",
                     "searchable":true,"page_size":5,"options":%s}]}' "$(esc "$current")" "$(options_json)"
            fi
            ;;
        esac
        ;;
    response)
        b=$(jget branch)
        send '{"kip":1,"type":"progress","value":null,"label":"Switching to %s…"}' "$(esc "$b")"
        sleep 0.8
        send '{"kip":1,"type":"done","title":"On branch %s","text":"Demo: nothing was really checked out.",
            "actions":[{"type":"copy","label":"Copy branch name","value":"%s"}]}' "$(esc "$b")" "$(esc "$b")"
        exit 0
        ;;
    esac
done

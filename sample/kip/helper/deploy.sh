#!/usr/bin/env bash
# KIP demo 1b — Deploy wizard written with the `kai kip` HELPER (compare with ../deploy.sh,
# which prints the same protocol by hand). The helper only formats messages and reads
# values out of Kai's answers; it needs no running app, but it does need the `kai`
# binary reachable from where this script runs.
set -u

# Find the helper: a Linux/macOS `kai`, or — when this runs inside WSL under a
# Windows-side Kai — the Windows kai.exe (set KAI_EXE to force a path).
if ! command -v kai >/dev/null 2>&1; then
    _kai=${KAI_EXE:-$(command -v kai.exe 2>/dev/null)}
    if [ -z "$_kai" ]; then
        for _p in "/mnt/c/Program Files/Kai/kai.exe" /mnt/c/Users/*/AppData/Local/Programs/Kai/kai.exe; do
            [ -x "$_p" ] && { _kai=$_p; break; }
        done
    fi
    [ -n "$_kai" ] || { echo "kai/kai.exe not found: set KAI_EXE to its path" >&2; exit 127; }
    kai() { "$_kai" "$@"; }
fi

# Waits for Kai's answer; a "cancel" means the user gave up.
read_response() {
    IFS= read -r RESP || exit 130
    [ "$(kai kip get "$RESP" type)" = cancel ] && exit 130
    return 0
}
answer() { kai kip get "$RESP" "$1"; }

kai kip hello --title "Deploy" --version "1.4.2"

while true; do
    kai kip prompt --id target --title "Where to deploy?" \
        --description "Pick the environment and a few options. Your last choices are remembered." \
        --submit-label "Next" \
        --field select env "Environment" --required --default staging \
            --option dev:Development --option staging:Staging:"Mirror of production" \
            --option prod:Production:"Customer facing" \
        --field flags opts "Options" \
            --flag dry:"Dry run":false --flag notify:"Notify the team":true
    read_response
    env=$(answer values.env)
    dry=$(answer values.opts.dry)

    if [ "$env" = prod ]; then
        kai kip confirm --id sure --title "Deploy to production?" \
            --text "Version 1.4.2 goes live for every customer. This cannot be undone from here." \
            --danger --confirm-label "Deploy now" --cancel-label "Not now" --back
        read_response
        [ "$(answer type)" = back ] && continue
        if [ "$(answer values.confirmed)" != true ]; then
            kai kip done --level warning --title "Deploy cancelled" --text "Nothing was deployed."
            exit 0
        fi
    fi
    break
done

suffix=''; [ "$dry" = true ] && suffix=' (dry run)'
kai kip message info "Deploying version 1.4.2 to ${env}${suffix}…"
for pct in 10 25 40 60 80 100; do
    kai kip progress "$pct" "Uploading artifacts… ${pct}%"
    sleep 0.5
done
kai kip done --title "Deployed to ${env}" --text "Version 1.4.2 is live${suffix}." \
    --action open_url:"Open the site":https://example.com \
    --action copy:"Copy release id":"rel-$(date +%s)"

#!/usr/bin/env bash
# The token must be "letmein" (try a wrong one to see "Ask again on retry").
[ "$TOKEN" = "letmein" ] || { echo "bad token" >&2; exit 1; }
mkdir -p .state
echo "deploying $(cat .state/artifact) to $TARGET..."; sleep 1
echo "r$(date +%s)" > .state/release
echo "release=$(cat .state/release)"
# Simulate a slow start: healthy only after a couple of seconds, unless FAIL_HEALTH is set.
( sleep 2; [ -n "$FAIL_HEALTH" ] || touch .state/healthy ) &

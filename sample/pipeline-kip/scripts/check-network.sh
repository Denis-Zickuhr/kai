#!/usr/bin/env bash
sleep 0.5
[ -z "$FAIL_NET" ] || { echo "cannot reach the registry" >&2; exit 1; }
echo "registry reachable"

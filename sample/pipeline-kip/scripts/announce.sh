#!/usr/bin/env bash
echo "announcing $VERSION to the team..."; sleep 0.5
[ -z "$FAIL_ANNOUNCE" ] || { echo "chat webhook unreachable" >&2; exit 1; }
echo "announced"

#!/usr/bin/env bash
# A tool that doesn't know KIP. Marked as KIP in kai.json on purpose: Kai shows
# the "This command doesn't support KIP" screen with this output in the log.
echo "not-kip: unknown option --kip" >&2
echo "usage: not-kip [--verbose]" >&2
exit 2

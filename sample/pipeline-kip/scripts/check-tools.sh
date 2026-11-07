#!/usr/bin/env bash
# Create .state/missing-tool to make this check fail.
[ ! -f .state/missing-tool ] || { echo "cmake not found" >&2; exit 1; }
echo "cmake, ninja, git found"

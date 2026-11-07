#!/usr/bin/env bash
mkdir -p .state
echo "building..."; sleep 1
echo "built-$(date +%s)" > .state/artifact
echo "artifact: $(cat .state/artifact)"

#!/usr/bin/env bash
# Builds a local Git repo from repo/ (catalog under dist/), tagged v1.0.0, to use as a "Git repo" provider.
set -euo pipefail
SRC="$(cd "$(dirname "$0")/../repo" && pwd)"
OUT=/tmp/kai-store-sample/github-demo
rm -rf "$OUT"
mkdir -p "$OUT"
cp -r "$SRC"/. "$OUT"/
cd "$OUT"
git init -q
git add -A
git -c user.name=demo -c user.email=demo@example.com commit -q -m "Acme dist store"
git tag v1.0.0
echo "Ready: $OUT"
echo "Add it in Settings -> Store: type Git repo, location $OUT"

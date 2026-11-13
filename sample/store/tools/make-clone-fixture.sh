#!/usr/bin/env bash
# Builds the local bare Git repo used by demo/app-by-reference (source.clone -> /tmp/kai-store-sample/app.git, tag v1.0.0).
set -euo pipefail
ROOT=/tmp/kai-store-sample
rm -rf "$ROOT"
mkdir -p "$ROOT/work"
cd "$ROOT/work"
git init -q
cat > kai.yml <<'YML'
project_name: "App by reference"
icon: "git-branch"
commands:
  - name: "Hello from the clone"
    type: "command"
    command: "echo this project lives in a cloned repo"
YML
git add kai.yml
git -c user.name=sample -c user.email=sample@example.com commit -q -m "App by reference"
git tag v1.0.0
git clone -q --bare . "$ROOT/app.git"
echo "Fixture ready: $ROOT/app.git (tag v1.0.0)"

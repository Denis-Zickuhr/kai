#!/usr/bin/env bash
# Builds bin/termbridge with the local Go, or with Docker if Go is not installed.
set -e
cd "$(dirname "$0")/termbridge"
mkdir -p ../bin
if command -v go >/dev/null; then
  CGO_ENABLED=0 go build -o ../bin/termbridge .
else
  docker run --rm -u "$(id -u):$(id -g)" -e CGO_ENABLED=0 -e GOCACHE=/tmp/gocache -v "$PWD/..:/src" -w /src/termbridge \
    golang:1.22-alpine go build -o ../bin/termbridge .
fi

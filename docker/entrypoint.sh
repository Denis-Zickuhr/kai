#!/usr/bin/env bash
set -e

if [ ! -d "/workspace/dist/dev" ]; then
  echo "[Kai Setup] Gerando build via CMake/Ninja..."
  cmake -B /workspace/dist/dev -G Ninja -DCMAKE_BUILD_TYPE=Debug -S /workspace
fi

exec "$@"
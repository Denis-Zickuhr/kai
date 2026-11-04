#!/usr/bin/env bash
# Wrapper script, like the user's: it runs the Go binary and reports its exit code.
cd "$(dirname "$0")" || exit 1
[ -x bin/termbridge ] || ./build.sh || exit 1
rm -rf "${KAI_STOP_DIR:-/tmp/kai-stop-test}"
echo "[run.sh] starting"
./bin/termbridge ./remote.sh
code=$?
echo "[run.sh] termbridge exited with $code"
exit $code

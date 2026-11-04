#!/usr/bin/env bash
# The "PHP script run over the terminal". Runs in the bridge's own session and
# ignores SIGHUP, so losing the bridge does NOT stop it (like a remote machine).
# ^C makes it clean up for CLEANUP_SECS and exit.
DIR="${KAI_STOP_DIR:-/tmp/kai-stop-test}"
CLEANUP_SECS="${CLEANUP_SECS:-4}"
mkdir -p "$DIR"
trap '' HUP
trap 'echo "[remote] ^C received, cleaning up for ${CLEANUP_SECS}s..."; sleep "$CLEANUP_SECS"; echo clean > "$DIR/status"; echo "[remote] clean exit"; exit 130' INT
echo running > "$DIR/status"
echo $$ > "$DIR/remote.pid"
i=0
while true; do
  i=$((i + 1))
  date +%s > "$DIR/heartbeat"
  echo "[remote] working... $i"
  sleep 1 &
  wait $!
done

#!/usr/bin/env bash
# After pressing Stop in Kai: is anything of the test still alive?
DIR="${KAI_STOP_DIR:-/tmp/kai-stop-test}"
sleep "${1:-0}"
echo "status file : $(cat "$DIR/status" 2>/dev/null || echo '-')   (clean = cleanup ran, running = it never got ^C)"
if [ -f "$DIR/heartbeat" ]; then
  age=$(( $(date +%s) - $(cat "$DIR/heartbeat") ))
  echo "heartbeat   : ${age}s ago   ($([ "$age" -le 2 ] && echo 'STILL RUNNING' || echo 'stopped'))"
fi
echo "processes   :"
pgrep -af 'termbridge|stop/remote.sh' | grep -v pgrep || echo "  none"
docker ps --filter label=com.docker.compose.project=kaistop --format '  container {{.Names}} {{.Status}}' 2>/dev/null

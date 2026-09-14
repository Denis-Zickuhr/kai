#!/usr/bin/env bash
# Emits log lines in the shapes Kai's formatted output (Grafana/Loki style) understands, so each
# case can be checked by hand. Usage: emit-logs.sh <mode> — see kai.yml for the commands that call it.
set -u

ts() { date -u +%Y-%m-%dT%H:%M:%SZ; }

# log <level> <message> [extra JSON fields, starting with a comma]
log() { printf '{"time":"%s","level":"%s","msg":"%s"%s}\n' "$(ts)" "$1" "$2" "${3:-}"; }

# lf <level> <message> [extra key=value pairs]: one logfmt line (the message is always quoted)
lf() { printf 'time=%s level=%s msg="%s"%s\n' "$(ts)" "$1" "$2" "${3:+ $3}"; }

pause() { sleep "${1:-0.15}"; }

mode_structured() {
  log debug "loading configuration" ',"service":"api","file":"/etc/app/config.yml"'; pause
  log info  "server listening" ',"service":"api","port":8080,"tls":false'; pause
  log info  "request completed" ',"service":"api","method":"GET","path":"/v1/users","status":200,"duration_ms":12.4,"request_id":"9f1c2e"'; pause
  log info  "request completed" ',"service":"api","method":"POST","path":"/v1/orders","status":201,"duration_ms":87.1,"request_id":"a41b77"'; pause
  log warn  "slow query" ',"service":"db","query":"SELECT * FROM orders WHERE status = ?","duration_ms":1840,"threshold_ms":500'; pause
  log info  "cache miss" ',"service":"api","key":"user:42","ttl_s":300'; pause
  log warn  "retrying upstream call" ',"service":"api","attempt":2,"max_attempts":5,"upstream":"payments"'; pause
  log error "upstream call failed" ',"service":"api","upstream":"payments","status":503,"error":"connection reset by peer"'; pause
  log info  "request completed" ',"service":"api","method":"GET","path":"/v1/health","status":200,"duration_ms":1.2'; pause
  log debug "gc finished" ',"service":"api","heap_mb":212,"pause_ms":4'; pause
  log info  "shutting down" ',"service":"api","reason":"SIGTERM"'
}

mode_mixed() {
  echo "=== booting worker (plain text banner) ==="
  echo "version 2.4.1, build 20260110"
  pause
  log info "worker started" ',"service":"worker","queue":"emails"'; pause
  echo "warning: deprecated option --legacy-mode is ignored"
  pause
  log info "job picked up" ',"service":"worker","job_id":"j-1001","type":"send_email"'; pause
  log error "job failed" ',"service":"worker","job_id":"j-1001","error":"smtp timeout"'; pause
  echo "Traceback (most recent call last):"
  echo "  File \"worker/mailer.py\", line 88, in send"
  echo "    conn.sendmail(sender, to, body)"
  echo "  File \"/usr/lib/python3.12/smtplib.py\", line 902, in sendmail"
  echo "TimeoutError: timed out"
  pause
  log info "job scheduled for retry" ',"service":"worker","job_id":"j-1001","retry_in_s":30'; pause
  echo "idle, waiting for jobs..."
}

mode_prefixed() {
  # Loggers that put a plain prefix (timestamp, level tag, service name) before the JSON object.
  printf '%s INFO  {"msg":"user signed in","user":"ana","ip":"203.0.113.9"}\n' "$(ts)"; pause
  printf '%s WARN  {"msg":"password attempt rejected","user":"bruno","attempts":3}\n' "$(ts)"; pause
  printf '[api] {"level":"info","message":"cache warmed","entries":1280}\n'; pause
  printf '[worker-2] {"level":"error","message":"lock timeout","table":"orders","waited_ms":5000}\n'; pause
  printf '%s DEBUG {"msg":"heartbeat","seq":1}\n' "$(ts)"
}

mode_shapes() {
  # Alternate key names, nested values, arrays, null/bool/number, escapes, unicode, a very long message.
  printf '{"@timestamp":"%s","severity":"INFO","message":"alternate key names (@timestamp/severity/message)"}\n' "$(ts)"; pause
  printf '{"ts":%s,"lvl":"warn","msg":"unix timestamp and lvl/msg keys"}\n' "$(date +%s)"; pause
  log info "nested object and array" ',"user":{"id":42,"name":"Ana Souza","roles":["admin","billing"],"profile":{"lang":"pt-BR","beta":true}},"tags":["a","b","c"],"parent":null'; pause
  # Written by hand: the message needs real JSON escapes (\" and \\) that log() does not add.
  printf '{"time":"%s","level":"info","msg":"unicode and escapes: ação ✓ — “aspas” \\"escaped\\" \\\\ backslash","emoji":"🚀","path":"C:\\\\temp\\\\file.txt"}\n' "$(ts)"; pause
  log warn "a very long message that should wrap inside the card instead of forcing horizontal scrolling: Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea commodo consequat." ',"service":"demo"'; pause
  log info "many fields" ',"a":1,"b":2,"c":3,"d":4,"e":5,"f":6,"g":7,"h":8,"i":9,"j":10,"k":11,"l":12'
}

mode_wrapped() {
  # A single JSON object physically broken across several lines (what some consoles do on long lines):
  # Kai should stitch it back into one card.
  printf '{"time":"%s","level":"info","msg":"this record was wra\n' "$(ts)"
  printf 'pped by the terminal in the middle of a word","servic\n'
  printf 'e":"api","path":"\\/var\\/log\\/\n'
  printf 'app\\/server.log","status":200}\n'
  pause
  log info "next record is normal" ',"service":"api"'
}

mode_error() {
  log info  "starting migration" ',"service":"db","from":41,"to":42'; pause
  log warn  "table is large, this may take a while" ',"table":"events","rows":48210334'; pause
  log error "migration step failed" ',"service":"db","step":"add_index","error":"deadlock detected"'; pause
  log fatal "aborting" ',"service":"db","exit_code":1'
  exit 1
}

mode_stream() {
  local count="${1:-60}" i level
  for ((i = 1; i <= count; i++)); do
    case $((i % 7)) in 0) level=warn ;; 3) level=debug ;; 5) level=error ;; *) level=info ;; esac
    log "$level" "tick $i of $count" ',"service":"stream","seq":'"$i"
    sleep 0.3
  done
}

mode_volume() {
  local count="${1:-5000}"
  awk -v n="$count" 'BEGIN {
    split("debug info info info warn error", lv, " ")
    for (i = 1; i <= n; i++)
      printf "{\"time\":\"2026-01-10T12:%02d:%02dZ\",\"level\":\"%s\",\"msg\":\"event %d\",\"seq\":%d,\"service\":\"bulk\"}\n", int(i / 60) % 60, i % 60, lv[(i % 6) + 1], i, i
  }'
}

mode_logfmt() {
  lf debug "loading configuration" 'service=api file=/etc/app/config.yml'; pause
  lf info  "server listening" 'service=api port=8080 tls=false'; pause
  lf info  "request completed" 'service=api method=GET path=/v1/users status=200 duration_ms=12.4 request_id=9f1c2e'; pause
  lf info  "request completed" 'service=api method=POST path=/v1/orders status=201 duration_ms=87.1 request_id=a41b77'; pause
  lf warn  "slow query" 'service=db duration_ms=1840 threshold_ms=500'; pause
  lf warn  "retrying upstream call" 'service=api attempt=2 max_attempts=5 upstream=payments'; pause
  lf error "upstream call failed" 'service=api upstream=payments status=503 error="connection reset by peer"'; pause
  lf info  "shutting down" 'service=api reason=SIGTERM'
}

mode_logfmt_mixed() {
  # logfmt, JSON and plain text interleaved: each line becomes a card on its own, the rest stays grouped text.
  echo "=== booting gateway (plain text banner) ==="
  pause
  lf info "gateway started" 'service=gateway port=9000'; pause
  log info "worker started" ',"service":"worker","queue":"emails"'; pause
  lf warn "deprecated header" 'service=gateway header=X-Legacy'; pause
  echo "note: this line has a = sign but is not logfmt, so it stays plain text"
  echo "retries = 3"
  pause
  log error "job failed" ',"service":"worker","job_id":"j-1001","error":"smtp timeout"'; pause
  lf error "job failed" 'service=gateway job_id=j-1001 error="smtp timeout"'
}

mode_logfmt_shapes() {
  # Key aliases, quoting, escapes, unicode, empty values, a bare level= with no msg, a very long message.
  printf 'ts=%s lvl=warn msg="alternate keys (ts/lvl/msg)"\n' "$(date +%s)"; pause
  printf 'severity=INFO message="quoted value with spaces and an = sign: a=b" user=ana\n'; pause
  printf 'time=%s level=info msg="escapes: \\"quoted\\" and a back\\\\slash" path="C:\\\\temp\\\\file.txt"\n' "$(ts)"; pause
  printf 'time=%s level=info msg="unicode: ação ✓ — 🚀" lang=pt-BR\n' "$(ts)"; pause
  printf 'time=%s level=debug msg="empty and unquoted values" empty= flag=true ratio=0.50 id=000123\n' "$(ts)"; pause
  printf 'level=error user=bruno error="no msg key: the card shows the pairs"\n'; pause
  lf warn "a very long message that should wrap inside the card instead of forcing horizontal scrolling: Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua." 'service=demo'
}

mode_logfmt_stream() {
  local count="${1:-60}" i level
  for ((i = 1; i <= count; i++)); do
    case $((i % 7)) in 0) level=warn ;; 3) level=debug ;; 5) level=error ;; *) level=info ;; esac
    lf "$level" "tick $i of $count" "service=stream seq=$i"
    sleep 0.3
  done
}

case "${1:-structured}" in
  structured) mode_structured ;;
  mixed)      mode_mixed ;;
  prefixed)   mode_prefixed ;;
  shapes)     mode_shapes ;;
  wrapped)    mode_wrapped ;;
  error)      mode_error ;;
  stream)     shift; mode_stream "$@" ;;
  volume)     shift; mode_volume "$@" ;;
  logfmt)         mode_logfmt ;;
  logfmt-mixed)   mode_logfmt_mixed ;;
  logfmt-shapes)  mode_logfmt_shapes ;;
  logfmt-stream)  shift; mode_logfmt_stream "$@" ;;
  *) echo "unknown mode: $1 (structured|mixed|prefixed|shapes|wrapped|error|stream|volume|logfmt|logfmt-mixed|logfmt-shapes|logfmt-stream)" >&2; exit 2 ;;
esac

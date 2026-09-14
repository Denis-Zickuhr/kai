#!/usr/bin/env bash
# Fixture de protocolo KIP em JSON cru (sem o helper `kai kip`). Usada por
# tests/test_kip_session.cpp. Cada linha recebida do Kai é acrescentada a
# KIP_TEST_OUT. Responde a `chip` com um chip_result "running" e depois com o
# resultado final (CHIP_FAIL=1 -> error). Termina quando chega o `response`.
emit() { printf "%s\n" "$1"; }
recv() { IFS= read -r line || exit 99; [ -n "${KIP_TEST_OUT:-}" ] && printf "%s\n" "$line" >> "$KIP_TEST_OUT"; }
emit '{"kip":1,"type":"hello"}'
emit '{"kip":1,"type":"prompt","id":"pick","fields":[{"name":"branch","type":"list","options":["main","feat"]},{"name":"token","type":"secret"}],"chips":[{"id":"ctx","label":"Context","requires":["branch"]},{"id":"nuke","danger":true,"confirm":{"text":"Really?"}}]}'
while true; do
  recv
  case "$line" in
    *'"type":"chip"'*)
      chip=$(printf '%s' "$line" | sed -n 's/.*"chip":"\([^"]*\)".*/\1/p')
      emit '{"kip":1,"type":"chip_result","chip":"'"$chip"'","state":"running","text":"working..."}'
      sleep 0.1
      if [ "${CHIP_FAIL:-0}" = 1 ]; then
        emit '{"kip":1,"type":"chip_result","chip":"'"$chip"'","state":"error","title":"Failed","text":"boom"}'
      else
        emit '{"kip":1,"type":"chip_result","id":"pick","chip":"'"$chip"'","state":"success","title":"Done","text":"all good"}'
      fi
      ;;
    *'"type":"response"'*) break ;;
  esac
done
emit '{"kip":1,"type":"done"}'

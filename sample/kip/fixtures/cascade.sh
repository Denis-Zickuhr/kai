#!/usr/bin/env bash
# Fixture de protocolo KIP em JSON cru (sem o helper `kai kip`). Usada por
# tests/test_kip_session.cpp. Se KIP_TEST_OUT estiver definido, cada linha
# recebida do Kai (stdin) é acrescentada a esse arquivo.
emit() { printf "%s\n" "$1"; }
recv() { IFS= read -r line || exit 99; [ -n "${KIP_TEST_OUT:-}" ] && printf "%s\n" "$line" >> "$KIP_TEST_OUT"; }
# STALE=1: responde primeiro com um patch de seq antigo (deve ser ignorado).
emit '{"kip":1,"type":"hello"}'
emit '{"kip":1,"type":"prompt","id":"k8s","fields":[{"name":"context","type":"select","options":["a","b"],"watch":true},{"name":"pod","type":"select","options":["old"]}]}'
recv
seq=$(printf '%s' "$line" | sed -n 's/.*"seq":\([0-9]*\).*/\1/p')
if [ "${STALE:-0}" = 1 ]; then
  emit '{"kip":1,"type":"patch","id":"k8s","seq":'"$((seq - 1))"',"fields":[{"name":"pod","type":"select","options":["stale"]}]}'
fi
emit '{"kip":1,"type":"patch","id":"k8s","seq":'"$seq"',"fields":[{"name":"pod","type":"select","options":["p1","p2"]}]}'
recv
emit '{"kip":1,"type":"done"}'

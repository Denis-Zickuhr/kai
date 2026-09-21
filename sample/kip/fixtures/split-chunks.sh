#!/usr/bin/env bash
# Fixture de protocolo KIP em JSON cru (sem o helper `kai kip`). Usada por
# tests/test_kip_session.cpp. Se KIP_TEST_OUT estiver definido, cada linha
# recebida do Kai (stdin) é acrescentada a esse arquivo.
emit() { printf "%s\n" "$1"; }
recv() { IFS= read -r line || exit 99; [ -n "${KIP_TEST_OUT:-}" ] && printf "%s\n" "$line" >> "$KIP_TEST_OUT"; }
printf '{"kip":1,"ty'
sleep 0.15
printf 'pe":"hello","title":"Spl'
sleep 0.15
printf 'it"}\n{"kip":1,"type":"mess'
sleep 0.15
printf 'age","level":"info","text":"one"}\n'
emit '{"kip":1,"type":"done"}'

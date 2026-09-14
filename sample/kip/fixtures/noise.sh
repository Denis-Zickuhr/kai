#!/usr/bin/env bash
# Fixture de protocolo KIP em JSON cru (sem o helper `kai kip`). Usada por
# tests/test_kip_session.cpp. Se KIP_TEST_OUT estiver definido, cada linha
# recebida do Kai (stdin) é acrescentada a esse arquivo.
emit() { printf "%s\n" "$1"; }
recv() { IFS= read -r line || exit 99; [ -n "${KIP_TEST_OUT:-}" ] && printf "%s\n" "$line" >> "$KIP_TEST_OUT"; }
echo "> pkg@1.0.0 deploy"
echo
emit '{"kip":1,"type":"hello","title":"Wrapped"}'
emit '{"kip":1,"type":"prompt","id":"p","fields":[]}'
recv
emit '{"kip":1,"type":"done"}'

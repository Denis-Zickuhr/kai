#!/usr/bin/env bash
# Fixture de protocolo KIP em JSON cru (sem o helper `kai kip`). Usada por
# tests/test_kip_session.cpp. Se KIP_TEST_OUT estiver definido, cada linha
# recebida do Kai (stdin) é acrescentada a esse arquivo.
emit() { printf "%s\n" "$1"; }
recv() { IFS= read -r line || exit 99; [ -n "${KIP_TEST_OUT:-}" ] && printf "%s\n" "$line" >> "$KIP_TEST_OUT"; }
emit '{"kip":1,"type":"hello"}'
emit '{"kip":1,"type":"prompt","id":"name","fields":[{"name":"name","type":"text","required":true}]}'
recv
emit '{"kip":1,"type":"invalid","id":"name","errors":{"name":"already taken"},"message":"Fix the name"}'
recv
emit '{"kip":1,"type":"done","title":"Created"}'

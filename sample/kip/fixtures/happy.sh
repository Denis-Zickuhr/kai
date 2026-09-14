#!/usr/bin/env bash
# Fixture de protocolo KIP em JSON cru (sem o helper `kai kip`). Usada por
# tests/test_kip_session.cpp. Se KIP_TEST_OUT estiver definido, cada linha
# recebida do Kai (stdin) é acrescentada a esse arquivo.
emit() { printf "%s\n" "$1"; }
recv() { IFS= read -r line || exit 99; [ -n "${KIP_TEST_OUT:-}" ] && printf "%s\n" "$line" >> "$KIP_TEST_OUT"; }
emit '{"kip":1,"type":"hello","title":"Deploy","version":"1.2.3"}'
emit '{"kip":1,"type":"prompt","id":"env","title":"Where to?","fields":[{"name":"env","type":"select","options":["dev","prod"],"default":"dev"},{"name":"token","type":"secret"}]}'
recv
emit '{"kip":1,"type":"confirm","id":"sure","text":"Deploy?","danger":true}'
recv
emit '{"kip":1,"type":"progress","value":50,"label":"Uploading"}'
echo "log line on stderr" >&2
emit '{"kip":1,"type":"notify","title":"Almost there","level":"info"}'
emit '{"kip":1,"type":"done","title":"Deployed","actions":[{"type":"open_url","label":"Open","url":"https://example.com"}]}'

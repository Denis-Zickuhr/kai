#!/usr/bin/env bash
# Fixture de protocolo KIP em JSON cru: uma tabela com ações por linha (§27). Cada linha recebida do Kai é
# acrescentada a KIP_TEST_OUT. Uma ação chega como `chip` com `field` e `row`; o programa responde com o
# chip_result e, no "delete", repinta a tabela sem a linha (patch espontâneo). Termina no `response`.
emit() { printf "%s\n" "$1"; }
recv() { IFS= read -r line || exit 99; [ -n "${KIP_TEST_OUT:-}" ] && printf "%s\n" "$line" >> "$KIP_TEST_OUT"; }
table() {
  printf '{"name":"branch","type":"table","row_key":"name","columns":["name"],"rows":[%s],"row_actions":[{"id":"sync"},{"id":"delete","danger":true,"confirm":{"text":"Sure?"}}]}' "$1"
}
emit '{"kip":1,"type":"hello"}'
emit '{"kip":1,"type":"prompt","id":"pick","fields":['"$(table '{"name":"main","_actions":["sync"]},{"name":"feat"}')"']}'
while true; do
  recv
  case "$line" in
    *'"chip":"delete"'*)
      emit '{"kip":1,"type":"patch","id":"pick","fields":['"$(table '{"name":"main","_actions":["sync"]}')"']}'
      emit '{"kip":1,"type":"chip_result","chip":"delete","state":"success","text":"deleted"}'
      ;;
    *'"type":"chip"'*) emit '{"kip":1,"type":"chip_result","chip":"sync","state":"success"}' ;;
    *'"type":"response"'*) break ;;
  esac
done
emit '{"kip":1,"type":"done"}'

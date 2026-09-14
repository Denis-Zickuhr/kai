#!/usr/bin/env bash
# Mostra no KIP o que o Kai injeta no ambiente do processo.
printf '%s\n' '{"kip":1,"type":"hello"}'
printf '%s\n' "{\"kip\":1,\"type\":\"message\",\"level\":\"info\",\"text\":\"KIP_VERSION=${KIP_VERSION} KIP_LOCALE=${KIP_LOCALE}\"}"
printf '%s\n' '{"kip":1,"type":"done"}'

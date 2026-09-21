#!/usr/bin/env bash
# Fixture: a sessão KIP mais curta possível — hello e done, sem pedir nada.
# Usada por tests/test_kip_main_window.cpp (fechar a janela ao terminar).
printf '%s\n' '{"kip":1,"type":"hello"}'
printf '%s\n' '{"kip":1,"type":"done","title":"Quick"}'

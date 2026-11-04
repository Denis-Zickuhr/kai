#!/usr/bin/env bash
# The smallest KIP program: the protocol is just JSON Lines on stdout
# (program -> Kai) and stdin (Kai -> program). The other demos in this folder
# add a few tiny bash functions (send/esc/jget/recv) on top of the same idea.
printf '%s\n' '{"kip":1,"type":"hello","title":"Raw protocol","version":"1"}'
printf '%s\n' '{"kip":1,"type":"prompt","id":"name","title":"Who are you?","fields":[{"name":"who","type":"text","label":"Name","required":true,"default":"world"}]}'
IFS= read -r response            # {"kip":1,"type":"response","id":"name","values":{"who":"..."}}
case "$response" in *'"type":"cancel"'*) exit 130 ;; esac
who=$(printf '%s' "$response" | sed -n 's/.*"who" *: *"\([^"]*\)".*/\1/p')
printf '%s\n' '{"kip":1,"type":"progress","value":50,"label":"Greeting…"}'
sleep 0.5
printf '%s\n' "{\"kip\":1,\"type\":\"done\",\"title\":\"Hello, ${who}!\",\"text\":\"Written without any helper.\"}"

#!/usr/bin/env bash
# Fails while a "dirty" marker exists. Create it to try the Retry / Skip / Abort choice: touch .state/dirty
[ ! -e .state/dirty ]

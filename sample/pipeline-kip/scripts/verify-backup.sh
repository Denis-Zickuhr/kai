#!/usr/bin/env bash
# BACKUP comes from the picker step. Pick "backup-corrupt.sql" to see the pipeline stop here.
echo "verifying $BACKUP..."; sleep 0.8
case "$BACKUP" in *corrupt*) echo "checksum mismatch" >&2; exit 1 ;; esac
echo "checksum ok"

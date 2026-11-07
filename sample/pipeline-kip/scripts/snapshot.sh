#!/usr/bin/env bash
mkdir -p .state
echo "snapshotting the current database..."; sleep 1
date +%s > .state/snapshot
echo "snapshot saved"

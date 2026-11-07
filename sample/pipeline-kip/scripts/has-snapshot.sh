#!/usr/bin/env bash
# exit 0 = a snapshot already exists, so the snapshot step is skipped.
[ -f .state/snapshot ]

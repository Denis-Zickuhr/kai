#!/usr/bin/env bash
echo "rolling back $(cat .state/release)"; rm -f .state/release .state/healthy

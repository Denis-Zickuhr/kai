#!/usr/bin/env bash
echo "disk: $(df -h . | awk 'NR==2 {print $4}') free"

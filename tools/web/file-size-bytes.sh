#!/usr/bin/env bash
# Portable byte count for build artifacts (Linux/macOS/CI).
set -euo pipefail
if [[ $# -ne 1 ]]; then
    echo "usage: $0 <path>" >&2
    exit 2
fi
wc -c < "$1" | tr -d '[:space:]'

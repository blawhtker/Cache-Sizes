#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/SIM"
TR="$ROOT/traces/MINIFE-1.t"

# quick slice: first 100k lines → feed via stdin
head -n 100000 "$TR" | "$BIN" 32768 4 0 1 /dev/stdin

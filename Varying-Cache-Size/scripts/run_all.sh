#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BIN="$ROOT/SIM"
TRACES_DIR="$ROOT/traces"
OUT_DIR="$ROOT/out"

mkdir -p "$OUT_DIR"

TRACES=("MINIFE-1.t" "XSBENCH-1.t")

parse_result() {
  awk '
    BEGIN{mr=""; w=""; r=""}
    /^Miss ratio/ {mr=$3}
    /^write/ {w=$2}
    /^read/  {r=$2}
    END{print mr","w","r}
  '
}

run_case() {
  local trace="$1" size="$2" assoc="$3" repl="$4" wb="$5"
  echo "[RUN] trace=$trace size=$size assoc=$assoc repl=$repl wb=$wb"
  "$BIN" "$size" "$assoc" "$repl" "$wb" "$TRACES_DIR/$trace" | parse_result
}

# ----- Part A: sizes 8KB..128KB, 4-way, LRU, WB -----
sizes=(8192 16384 32768 65536 131072)
assoc_fixed=4
repl_lru=0
for t in "${TRACES[@]}"; do
  csv="$OUT_DIR/partA_${t%.t}.csv"
  echo "trace,size_bytes,assoc,replacement,wb,miss_ratio,mem_writes,mem_reads" > "$csv"
  for s in "${sizes[@]}"; do
    echo "$t,$s,$assoc_fixed,LRU,WB,$(run_case "$t" "$s" "$assoc_fixed" "$repl_lru" 1)" >> "$csv"
  done
done

# ----- Part B: WB vs WT, same sizes, 4-way, LRU -----
for t in "${TRACES[@]}"; do
  csv="$OUT_DIR/partB_${t%.t}.csv"
  echo "trace,size_bytes,assoc,replacement,wb,miss_ratio,mem_writes,mem_reads" > "$csv"
  for s in "${sizes[@]}"; do
    echo "$t,$s,$assoc_fixed,LRU,WB,$(run_case "$t" "$s" "$assoc_fixed" "$repl_lru" 1)" >> "$csv"
    echo "$t,$s,$assoc_fixed,LRU,WT,$(run_case "$t" "$s" "$assoc_fixed" "$repl_lru" 0)" >> "$csv"
  done
done

# ----- Part C: assoc 1..64, 32KB, LRU, WB -----
assocs=(1 2 4 8 16 32 64)
size_fixed=32768
for t in "${TRACES[@]}"; do
  csv="$OUT_DIR/partC_${t%.t}.csv"
  echo "trace,size_bytes,assoc,replacement,wb,miss_ratio,mem_writes,mem_reads" > "$csv"
  for a in "${assocs[@]}"; do
    echo "$t,$size_fixed,$a,LRU,WB,$(run_case "$t" "$size_fixed" "$a" "$repl_lru" 1)" >> "$csv"
  done
done

# ----- Part D: FIFO vs LRU, 32KB, WB -----
repl_fifo=1
for t in "${TRACES[@]}"; do
  csv="$OUT_DIR/partD_${t%.t}.csv"
  echo "trace,size_bytes,assoc,replacement,wb,miss_ratio,mem_writes,mem_reads" > "$csv"
  for a in "${assocs[@]}"; do
    echo "$t,$size_fixed,$a,FIFO,WB,$(run_case "$t" "$size_fixed" "$a" "$repl_fifo" 1)" >> "$csv"
  done
done

echo "All done. See CSVs in: $OUT_DIR"

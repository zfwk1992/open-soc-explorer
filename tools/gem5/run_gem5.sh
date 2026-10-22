#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Part of Open SoC Explorer.
#
# Runs one of the RISC-V workloads on gem5 and converts the result into an
# Open SoC Explorer trace.
#
#   GEM5=/path/to/gem5 tools/gem5/run_gem5.sh <workload> <cpu> <caches> <outdir> [se_trace.py options]
#
#   workload  loop_sum | array_sum | array_sum_seq (tools/gem5/workloads)
#   cpu       atomic | timing | o3
#   caches    none | l1 | l1l2
#   options   e.g. --l1-size 4KiB --l2-size 256KiB --clk 2GHz
#
# Requires a RISC-V build of gem5 ($GEM5/build/RISCV/gem5.opt) and a RISC-V
# cross compiler (RISCV_CC, default riscv64-linux-gnu-gcc). Writes the gem5
# output to <outdir>/m5out and the trace to <outdir>/trace.jsonl.
set -euo pipefail

if [[ $# -lt 4 ]]; then
  sed -n '5,17p' "$0"
  exit 2
fi
workload=$1
cpu=$2
caches=$3
outdir=$4
shift 4

here=$(cd "$(dirname "$0")" && pwd)
: "${GEM5:?set GEM5 to the gem5 source directory}"
gem5_bin="$GEM5/build/RISCV/gem5.opt"
[[ -x $gem5_bin ]] || { echo "error: $gem5_bin not found" >&2; exit 1; }

make -s -C "$here/workloads" "$workload.elf"
elf="$here/workloads/$workload.elf"

flags=ExecEnable,ExecUser,ExecOpClass,ExecResult,ExecEffAddr,ExecFetchSeq
[[ $cpu == o3 ]] && flags="$flags,O3PipeView"

mkdir -p "$outdir"
"$gem5_bin" --outdir="$outdir/m5out" --debug-flags="$flags" \
  --debug-file=trace.out \
  "$here/se_trace.py" --cpu "$cpu" --caches "$caches" "$@" "$elf" \
  > "$outdir/gem5.log" 2>&1 || {
  echo "error: gem5 failed, see $outdir/gem5.log" >&2
  exit 1
}

version=$(git -C "$GEM5" describe --tags --always 2>/dev/null ||
  sed -n 's/^.*gem5 version \([^ ]*\).*$/\1/p' "$outdir/gem5.log" | head -1)
python3 "$here/gem5_to_osx.py" --trace "$outdir/m5out/trace.out" \
  --m5out "$outdir/m5out" --binary "$elf" --gem5-version "${version:-unknown}" \
  -o "$outdir/trace.jsonl"
python3 "$here/../trace/osx_trace.py" summary "$outdir/trace.jsonl"

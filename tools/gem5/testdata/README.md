# Recorded gem5 output (test data)

Real, unmodified gem5 output, gzip-compressed (`gzip -n -9`), used by
`tools/gem5/test_gem5_to_osx.py` and by the C++ tests (`tst_trace`,
`tst_explorergui` convert it with `gem5_to_osx.py` at test time).

| Directory | Command |
|---|---|
| `loop_sum_o3_l1l2/` | `run_gem5.sh loop_sum o3 l1l2 …` |
| `loop_sum_timing_l1/` | `run_gem5.sh loop_sum timing l1 …` |

Each directory holds `trace.out.gz` (gem5 `--debug-file`), `config.json.gz`
and `stats.txt.gz` from the gem5 output directory.

* gem5 v26.0.0.0 (commit `fed0e07`), `build/RISCV/gem5.opt`, built with
  GCC 13.3 on Ubuntu 24.04.
* `loop_sum.elf`: `tools/gem5/workloads/loop_sum.S` built with
  `riscv64-linux-gnu-gcc` 13.3.0 (`make -C tools/gem5/workloads`),
  SHA-256 `db8d563c526dd50eed5be6e366601c24935c4b4714934a6da7f358ea85a5f872`.

`config.json` contains the absolute paths of the machine the runs were made
on (`cwd`, `executable`); the converter does not use them.

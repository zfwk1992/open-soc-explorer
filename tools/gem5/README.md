# gem5 tooling

Run RISC-V programs on [gem5](https://www.gem5.org/) and open the result
in the Open SoC Explorer (*Explorer* tab → *Open a trace file for offline
replay…*): instruction timeline, out-of-order pipeline occupancy, register
values and the *System* view with the simulated caches, prefetchers,
crossbars and DRAM and their gem5 statistics.

gem5 is an external tool: nothing from gem5 is vendored or linked here
(see [docs/LICENSING.md](../../docs/LICENSING.md)). Tested with gem5
**v26.0.0.0** (commit `fed0e07`).

| File | Purpose |
|---|---|
| `se_trace.py` | gem5 configuration (gem5 standard library): `SimpleBoard`, one RISC-V core (`--cpu atomic\|timing\|o3`), cache hierarchy (`--caches none\|l1\|l1l2`, `--l1-size`, `--l2-size`), DDR3-1600 memory, `--clk` |
| `gem5_to_osx.py` | Converts gem5's debug trace, `config.json` and `stats.txt` into a trace (schema 0.2.0). Python standard library only |
| `run_gem5.sh` | Builds a workload, runs gem5 with the right debug flags, converts, prints a summary |
| `workloads/` | `loop_sum` (the Mission 3 loop), `array_sum` / `array_sum_seq` (same 2048 loads, strided or sequential) |
| `testdata/` | Recorded gem5 output used by the tests (see its README) |
| `test_gem5_to_osx.py` | Converter tests: `python3 -m unittest discover -s tools/gem5` |

## 1. Build gem5 (once)

On Ubuntu 24.04 (this is how it was built for the tests; see the
[gem5 documentation](https://www.gem5.org/documentation/general_docs/building)
for other systems):

```bash
sudo apt install build-essential scons python3-dev m4 zlib1g-dev \
    libprotobuf-dev protobuf-compiler libgoogle-perftools-dev libpng-dev \
    gcc-riscv64-linux-gnu
git clone --branch v26.0.0.0 --depth 1 https://github.com/gem5/gem5.git
cd gem5 && scons build/RISCV/gem5.opt -j"$(nproc)"
```

The build takes about an hour on 4 cores and needs ~7 GB of disk.

## 2. Run and convert

```bash
GEM5=/path/to/gem5 tools/gem5/run_gem5.sh loop_sum o3 l1l2 out/loop_o3
# then open out/loop_o3/trace.jsonl in the Explorer
```

Manually, the steps are:

```bash
$GEM5/build/RISCV/gem5.opt --outdir=m5out \
    --debug-flags=ExecEnable,ExecUser,ExecOpClass,ExecResult,ExecEffAddr,ExecFetchSeq,O3PipeView \
    --debug-file=trace.out \
    tools/gem5/se_trace.py --cpu o3 --caches l1l2 prog.elf
python3 tools/gem5/gem5_to_osx.py --trace m5out/trace.out --m5out m5out \
    --binary prog.elf --gem5-version v26.0.0.0 -o trace.jsonl
```

`O3PipeView` is needed for `--cpu o3` only. `MinorCPU` is not supported yet
(the converter rejects it).

## 3. What the converter reports, and how

Everything is labelled with an observability level (see
[docs/SIMULATION_FIDELITY.md §6](../../docs/SIMULATION_FIDELITY.md)):

* **O3CPU**: fetch, decode, rename, dispatch, issue, complete and commit
  times of every dynamic instruction (`O3PipeView`, *direct*); wrong-path
  instructions (never committed) as squashes whose time is *inferred*.
* **TimingSimpleCPU / AtomicSimpleCPU**: the tick at which each instruction
  was decoded and started executing (*direct*); completion is *inferred*
  from the start of the next instruction.
* Register results (`D=`) and data addresses (`A=`) of committed
  instructions.
* `config.json` → simulated components with parameters (plus their meaning
  and unit) and connections; `stats.txt` → statistics with gem5's own
  descriptions.
* Time: `time.tick` in gem5 ticks (1 ps); `time.cycle` = tick / CPU clock
  period.

Not available from these outputs: initial register values (shown as
*unknown* until written), per-access cache hits/misses, bus transactions,
DRAM commands.

## 4. Results measured with these workloads

gem5 v26.0.0.0, 1 GHz, DDR3-1600, gem5 standard-library caches (each with a
stride prefetcher):

| Workload | CPU | Caches | Cycles | Committed instructions |
|---|---|---|---|---|
| `loop_sum` | O3 | L1 16 KiB + L2 256 KiB | 248 | 35 |
| `loop_sum` | TimingSimple | L1 16 KiB | 163 | 35 |
| `loop_sum` | AtomicSimple | none | 35 | 35 |

All three end with `a0 = 55` and commit the same instruction stream as
Ripes. gem5 counts 35 instructions while Ripes retires 36: the final exit
`ecall` ends the gem5 simulation and is not counted
(`commitStats0.numInsts 35`).

Cache locality (O3, L1 + L2, same 2048 four-byte loads of an 8 KiB array):

| Access order | L1D size | L1D hits / misses | Cycles |
|---|---|---|---|
| strided (64 bytes apart) | 16 KiB | 1920 / 128 | 3272 |
| sequential | 16 KiB | 1929 / 119 | 2601 |
| strided | 4 KiB | 18 / 2030 | 6939 |
| sequential | 4 KiB | 1929 / 119 | 2601 |

With a 4 KiB L1D the strided order misses on almost every load and takes
2.7× the cycles of the sequential order, whose cost does not depend on the
L1 size. The L1D prefetcher issued 484 prefetches in the sequential 16 KiB
run, 120 of them useful (`prefetcher.pfIssued`, `pfUseful`). These are
properties of this gem5 configuration, not of a particular chip.

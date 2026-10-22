# SPDX-License-Identifier: MIT
# Part of Open SoC Explorer.
"""gem5 configuration: run a static RISC-V Linux binary in syscall-emulation
(SE) mode on a simple board, for use with gem5_to_osx.py.

Run with a RISC-V gem5 build, for example:

    build/RISCV/gem5.opt --outdir=m5out \
        --debug-flags=ExecEnable,ExecUser,ExecOpClass,ExecResult,ExecEffAddr,ExecFetchSeq,O3PipeView \
        --debug-file=trace.out \
        tools/gem5/se_trace.py --cpu o3 --caches l1l2 prog.elf

The board, processor, cache hierarchy and memory come from the gem5 standard
library (gem5.components). Tested with gem5 v26.0.0.0.
"""

import argparse

from gem5.components.boards.simple_board import SimpleBoard
from gem5.components.cachehierarchies.classic.no_cache import NoCache
from gem5.components.cachehierarchies.classic.private_l1_cache_hierarchy import (
    PrivateL1CacheHierarchy,
)
from gem5.components.cachehierarchies.classic.private_l1_private_l2_cache_hierarchy import (
    PrivateL1PrivateL2CacheHierarchy,
)
from gem5.components.memory import SingleChannelDDR3_1600
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import BinaryResource
from gem5.simulate.simulator import Simulator

parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
parser.add_argument("binary", help="static RISC-V Linux ELF")
parser.add_argument(
    "--cpu",
    choices=["atomic", "timing", "minor", "o3"],
    default="timing",
    help="gem5 CPU model",
)
parser.add_argument(
    "--caches",
    choices=["none", "l1", "l1l2"],
    default="l1",
    help="classic cache hierarchy",
)
parser.add_argument("--clk", default="1GHz", help="board and CPU clock")
parser.add_argument("--l1-size", default="16KiB")
parser.add_argument("--l2-size", default="256KiB")
args = parser.parse_args()

cpu_types = {
    "atomic": CPUTypes.ATOMIC,
    "timing": CPUTypes.TIMING,
    "minor": CPUTypes.MINOR,
    "o3": CPUTypes.O3,
}

if args.caches == "none":
    cache_hierarchy = NoCache()
elif args.caches == "l1":
    cache_hierarchy = PrivateL1CacheHierarchy(
        l1d_size=args.l1_size, l1i_size=args.l1_size
    )
else:
    cache_hierarchy = PrivateL1PrivateL2CacheHierarchy(
        l1d_size=args.l1_size, l1i_size=args.l1_size, l2_size=args.l2_size
    )

processor = SimpleProcessor(
    cpu_type=cpu_types[args.cpu], num_cores=1, isa=ISA.RISCV, clk_freq=args.clk
)
board = SimpleBoard(
    clk_freq=args.clk,
    processor=processor,
    memory=SingleChannelDDR3_1600(size="512MiB"),
    cache_hierarchy=cache_hierarchy,
)
board.set_se_binary_workload(BinaryResource(local_path=args.binary))

simulator = Simulator(board=board)
simulator.run()
print(f"Exited at tick {simulator.get_current_tick()}: {simulator.get_last_exit_event_cause()}")

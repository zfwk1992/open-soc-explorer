#pragma once

/**
 * @file simtgpu.h
 * @brief A small, Qt-free SIMT (Single Instruction, Multiple Threads) GPU
 * model intended for teaching how a GPU executes a kernel: warps, lanes,
 * active masks, branch divergence/reconvergence, barriers, memory
 * coalescing, shared-memory bank conflicts and latency hiding through warp
 * scheduling.
 *
 * The model is deliberately simple:
 *  - A GPU consists of `numSMs` streaming multiprocessors (SMs).
 *  - Each SM owns `warpsPerSM` warp slots; each warp has `warpSize` lanes
 *    (threads). Every lane owns 16 32-bit registers R0..R15 (R0 reads as 0 and
 *    ignores writes) and 4 predicate bits P0..P3.
 *  - A kernel is launched as a 1-D grid of `gridX` thread blocks, each with
 *    `blockX` threads. A block is split into ceil(blockX / warpSize) warps
 *    which are all resident on the same SM (required for barriers and shared
 *    memory). Blocks are dispatched to SMs round-robin whenever enough warp
 *    slots are free.
 *  - Every call to step() advances all SMs by one cycle. Each SM issues at
 *    most one warp-instruction per cycle, picked by a loose round-robin
 *    scheduler among the warps that are ready (not stalled on memory, not
 *    waiting at a barrier). Cycles in which no warp is ready are counted as
 *    idle issue slots.
 *  - Global memory accesses go through two std::function callbacks so that the
 *    model can be wired to any bus (e.g. the Ripes memory). A global load
 *    stalls the issuing warp for `memLatency + (transactions - 1)` cycles,
 *    where `transactions` is the number of distinct 32-byte segments touched by
 *    the active lanes (memory coalescing). Other warps may issue meanwhile:
 *    this is the latency hiding mechanism of real GPUs.
 *  - Divergence is handled with a "min-PC" reconvergence rule: a warp keeps a
 *    set of (pc, mask) paths; the path with the smallest pc is always executed
 *    first, and paths that reach the same pc are merged. For structured code
 *    (if/else, loops) this reconverges at the immediate post-dominator without
 *    requiring explicit JOIN/SSY instructions.
 *  - Barriers count *warps*, not paths: a path that executes BAR is parked
 *    (it stops issuing) until every live lane of its warp has either reached
 *    a BAR or exited. Only then does the warp arrive at the block barrier,
 *    exactly once. When all live warps of the block have arrived the barrier
 *    releases and the parked paths resume (and reconverge where possible).
 *  - Memory callbacks may report a bus error through fault(); the kernel then
 *    stops with an error at the end of the current instruction.
 *
 * ---------------------------------------------------------------------------
 * Kernel ISA (32-bit fixed-length instruction words)
 * ---------------------------------------------------------------------------
 *
 *   bit  31  30  29..28  27..22  21..18  17..14  13..10  9..6   5..0
 *       +---+---+-------+-------+-------+-------+-------+------+------+
 *  R:   |GE |GN | GP    |opcode |  rd   |  rs1  |  rs2  | rs3  |funct |
 *  I:   |GE |GN | GP    |opcode |  rd   |  rs1  |         imm14        |
 *  P:   |GE |GN | GP    |opcode | pidx  |  rs1  | cond  |    imm10     |
 *  B:   |GE |GN | GP    |opcode |              target22               |
 *       +---+---+-------+-------+-------+-------+-------+------+------+
 *
 *  Guard (bits 31..28, all forms): GE = guard enable, GN = negate,
 *    GP = predicate register index. When GE=1 the instruction is only executed
 *    by lanes whose predicate P[GP] (xor GN) is true. Written as `@P1` /
 *    `@!P1` in front of the mnemonic. A guarded instruction with no eligible
 *    lanes is skipped (still consumes an issue slot).
 *  imm14 / imm10 are sign-extended. target22 is an absolute instruction
 *    index (word index relative to the kernel base address).
 *  funct (R form) holds the compare condition for SETP (otherwise 0).
 *  For STG/STS (I form) the `rd` field holds the register whose value is
 *    stored.
 *
 *  Opcode table (decimal value in bits 27..22):
 *
 *   0  NOP                                            -
 *   1  EXIT                 lane terminates           -
 *   2  BAR                  block-wide barrier        -
 *   3  BRA  target          B   pc = target (for lanes passing the guard;
 *                               other lanes fall through -> divergence)
 *   4  MOVS rd, sreg        I   rd = special register (imm14 = sreg id)
 *                               0 TID, 1 BID, 2 NTID (blockDim), 3 NBID
 *                               (gridDim), 4..7 ARG0..ARG3, 8 LANEID,
 *                               9 WARPID (warp within block), 10 SMID
 *   8  ADD  rd, rs1, rs2    R   rd = rs1 + rs2
 *   9  SUB  rd, rs1, rs2    R   rd = rs1 - rs2
 *  10  MUL  rd, rs1, rs2    R   rd = rs1 * rs2 (low 32 bits)
 *  11  FMA  rd, rs1, rs2, rs3  R  rd = rs1 * rs2 + rs3
 *  12  AND  rd, rs1, rs2    R
 *  13  OR   rd, rs1, rs2    R
 *  14  XOR  rd, rs1, rs2    R
 *  15  SHL  rd, rs1, rs2    R   logical shift left by (rs2 & 31)
 *  16  SHR  rd, rs1, rs2    R   logical shift right
 *  17  SRA  rd, rs1, rs2    R   arithmetic shift right
 *  24  ADDI rd, rs1, imm    I
 *  25  MULI rd, rs1, imm    I
 *  26  ANDI rd, rs1, imm    I
 *  27  ORI  rd, rs1, imm    I
 *  28  XORI rd, rs1, imm    I
 *  29  SHLI rd, rs1, imm    I
 *  30  SHRI rd, rs1, imm    I
 *  31  SRAI rd, rs1, imm    I
 *  32  SETP.cond pd, rs1, rs2   R  pd = (rs1 cond rs2); cond in funct
 *  33  SETP.cond pd, rs1, imm   P  pd = (rs1 cond imm10); cond in bits 13..10
 *  40  LDG  rd, [rs1+imm]   I   rd = global[rs1 + imm]      (32-bit)
 *  41  STG  [rs1+imm], rd   I   global[rs1 + imm] = rd
 *  42  LDS  rd, [rs1+imm]   I   rd = shared[rs1 + imm]      (block-local)
 *  43  STS  [rs1+imm], rd   I   shared[rs1 + imm] = rd
 *
 *  Conditions (cond / funct): 0 EQ, 1 NE, 2 LT, 3 GE, 4 LTU, 5 GEU, 6 LE,
 *  7 GT (LT/GE/LE/GT are signed).
 *
 *  Assembler syntax (see assembleKernel()):
 *    - one instruction per line, `#` or `//` starts a comment,
 *      `label:` defines a label (usable as BRA target),
 *    - registers R0..R15, predicates P0..P3, immediates decimal or 0x-hex,
 *    - memory operands `[R4]`, `[R4+8]`, `[R4-4]`,
 *    - `ADD R1, R2, 5` automatically selects the immediate form (ADDI);
 *      `SUB R1, R2, 5` becomes `ADDI R1, R2, -5`,
 *    - pseudo instructions: `MOV rd, rs` (ADD rd, rs, R0),
 *      `MOV rd, imm` (ADDI rd, R0, imm), `S_TID rd`, `S_BID rd`, `S_NTID rd`,
 *      `S_NBID rd`, `S_LANE rd`, `S_WARP rd`, `S_SMID rd`, `S_ARG rd, k`
 *      (all MOVS), `@P0 BRA label`, `@!P0 EXIT`.
 */

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Ripes {
namespace gpu {

// ---------------------------------------------------------------------------
// ISA definitions
// ---------------------------------------------------------------------------

enum class Opcode : uint8_t {
  NOP = 0,
  EXIT = 1,
  BAR = 2,
  BRA = 3,
  MOVS = 4,
  ADD = 8,
  SUB = 9,
  MUL = 10,
  FMA = 11,
  AND = 12,
  OR = 13,
  XOR = 14,
  SHL = 15,
  SHR = 16,
  SRA = 17,
  ADDI = 24,
  MULI = 25,
  ANDI = 26,
  ORI = 27,
  XORI = 28,
  SHLI = 29,
  SHRI = 30,
  SRAI = 31,
  SETP = 32,
  SETPI = 33,
  LDG = 40,
  STG = 41,
  LDS = 42,
  STS = 43,
};

enum class Cond : uint8_t { EQ = 0, NE, LT, GE, LTU, GEU, LE, GT };

enum class SReg : uint8_t {
  TID = 0,
  BID = 1,
  NTID = 2,
  NBID = 3,
  ARG0 = 4,
  ARG1 = 5,
  ARG2 = 6,
  ARG3 = 7,
  LANEID = 8,
  WARPID = 9,
  SMID = 10
};

/// Instruction forms, derived from the opcode.
enum class Form { R, I, P, B, NONE };

/// A decoded instruction.
struct Instr {
  uint32_t raw = 0;
  bool valid = false; ///< false if the opcode is not defined
  Opcode op = Opcode::NOP;
  Form form = Form::NONE;
  bool guardEn = false;
  bool guardNeg = false;
  uint8_t guardP = 0;
  uint8_t rd = 0;
  uint8_t rs1 = 0;
  uint8_t rs2 = 0;
  uint8_t rs3 = 0;
  uint8_t funct = 0;
  int32_t imm = 0;     ///< sign-extended imm14 (I) or imm10 (P)
  uint32_t target = 0; ///< B form absolute instruction index
};

/// Static properties of one opcode value. A single table drives decoding,
/// validity checks and mnemonics.
struct OpcodeInfo {
  const char *name = nullptr; ///< nullptr for undefined opcode values
  Form form = Form::NONE;
  bool defined() const { return name != nullptr; }
};
/// Table lookup for the 6-bit opcode field (values >= 64 are undefined).
const OpcodeInfo &opcodeInfo(unsigned op);
/// Returns the form of @p op (Form::NONE for undefined opcodes).
Form opcodeForm(unsigned op);
/// Mnemonic for an opcode (never nullptr; "???" for undefined values).
const char *opcodeName(Opcode op);
const char *condName(Cond c);
const char *sregName(SReg s);

/// Decodes an instruction word.
Instr decode(uint32_t word);

/// Encoding helpers (used by the assembler and the tests).
uint32_t encodeR(Opcode op, unsigned rd, unsigned rs1, unsigned rs2,
                 unsigned rs3 = 0, unsigned funct = 0, unsigned guard = 0);
uint32_t encodeI(Opcode op, unsigned rd, unsigned rs1, int32_t imm14,
                 unsigned guard = 0);
uint32_t encodeP(unsigned pidx, unsigned rs1, Cond cond, int32_t imm10,
                 unsigned guard = 0);
uint32_t encodeB(uint32_t target22, unsigned guard = 0);
/// Builds the 4-bit guard nibble (bits 31..28) for `@P{p}` / `@!P{p}`.
unsigned makeGuard(bool enable, bool negate, unsigned pidx);

/// Disassembles an instruction word into assembler syntax, e.g.
/// "@!P1 ADD R1, R2, R3" or "LDG R5, [R4+8]".
std::string disassemble(uint32_t word);

/**
 * @brief assembleKernel
 * Assembles kernel source (see the syntax description at the top of this
 * file) into instruction words. On error, if @p error is non-null, the message
 * (prefixed with the line number) is stored there and an empty vector is
 * returned; if @p error is null a std::runtime_error is thrown.
 */
std::vector<uint32_t> assembleKernel(const std::string &source,
                                     std::string *error = nullptr);

// ---------------------------------------------------------------------------
// Microarchitecture model
// ---------------------------------------------------------------------------

struct GpuConfig {
  unsigned numSMs = 2;
  unsigned warpsPerSM = 4;
  unsigned warpSize = 8;       ///< 1..32 lanes
  unsigned memLatency = 4;     ///< cycles a warp waits for a global load
  unsigned sharedBytes = 1024; ///< shared memory per block
  unsigned sharedBanks = 8;    ///< number of (4-byte wide) shared memory banks
};

struct GpuStats {
  uint64_t cycles = 0;             ///< GPU cycles since launch
  uint64_t issued = 0;             ///< warp instructions issued (all SMs)
  uint64_t idleSlots = 0;          ///< SM-cycles in which nothing could issue
  uint64_t divergences = 0;        ///< branches that split a warp in two paths
  uint64_t memTransactions = 0;    ///< 32-byte global memory segments accessed
  uint64_t memInstrs = 0;          ///< LDG/STG warp instructions executed
  /// LDG/STG instructions that touched the minimum possible number of 32-byte
  /// segments (ceil(active lanes * 4 / 32)), and their transactions.
  uint64_t coalescedInstrs = 0;
  uint64_t coalescedTxns = 0;
  /// LDG/STG instructions that needed more segments than that minimum, and
  /// their transactions (coalescedTxns + uncoalescedTxns = memTransactions).
  uint64_t uncoalescedInstrs = 0;
  uint64_t uncoalescedTxns = 0;
  uint64_t bankConflicts = 0;      ///< LDS/STS instructions with a conflict
  uint64_t bankConflictCycles = 0; ///< extra cycles due to shared bank conflicts
  uint64_t barriers = 0;           ///< warp arrivals at a block barrier
  uint64_t blocksLaunched = 0;
  uint64_t blocksCompleted = 0;
  uint64_t threadInstrs = 0; ///< sum of executing lanes over issued instrs
};

struct LaunchParams {
  uint32_t kernelPc = 0; ///< byte address of the first instruction
  uint32_t gridX = 1;    ///< number of thread blocks
  uint32_t blockX = 1;   ///< threads per block
  std::array<uint32_t, 4> args = {{0, 0, 0, 0}};
};

/// One control-flow path of a warp: a program counter and the lanes that
/// follow it.
struct WarpPath {
  uint32_t pc = 0;
  uint32_t mask = 0;
  /// The lanes executed BAR and wait (pc already points past the BAR) until
  /// their whole warp has arrived and the block barrier releases.
  bool parked = false;
};

struct WarpState {
  bool valid = false;   ///< slot holds a live warp
  uint32_t blockId = 0; ///< blockIdx.x
  uint32_t warpInBlock = 0;
  uint32_t validMask = 0;  ///< lanes that correspond to real threads
  uint32_t exitedMask = 0; ///< lanes that executed EXIT
  /// Pending control-flow paths, sorted by (pc, parked). The first path that
  /// is not parked at a barrier executes next; its pc/mask are mirrored by
  /// pc()/activeMask() below.
  std::vector<WarpPath> paths;
  unsigned stallCycles = 0; ///< remaining cycles before the warp may issue
  bool atBarrier = false;   ///< whole warp arrived at BAR, waiting for block
  bool hasLastInstr = false;
  uint32_t lastInstr = 0;     ///< last issued instruction word
  uint32_t lastPc = 0;        ///< pc of lastInstr
  uint32_t lastExecMask = 0;  ///< lanes that actually executed lastInstr
  std::vector<uint32_t> regs; ///< lane-major: regs[lane * 16 + r]
  std::vector<uint8_t> preds; ///< per lane, bit i = P[i]

  /// Index of the path that executes next (first non-parked path), or -1 if
  /// every path is parked at a barrier (or the warp finished).
  int currentPath() const {
    for (size_t i = 0; i < paths.size(); ++i)
      if (!paths[i].parked)
        return static_cast<int>(i);
    return -1;
  }
  uint32_t pc() const {
    const int i = currentPath();
    if (i >= 0)
      return paths[static_cast<size_t>(i)].pc;
    return paths.empty() ? 0 : paths.front().pc;
  }
  uint32_t activeMask() const {
    const int i = currentPath();
    return i >= 0 ? paths[static_cast<size_t>(i)].mask : 0u;
  }
  /// Lanes parked at a barrier.
  uint32_t parkedMask() const {
    uint32_t m = 0;
    for (const auto &p : paths)
      if (p.parked)
        m |= p.mask;
    return m;
  }
  /// True when the warp is split over more than one control-flow path.
  bool diverged() const { return paths.size() > 1; }
  bool finished() const { return paths.empty(); }
};

struct BlockState {
  uint32_t blockId = 0;
  unsigned liveWarps = 0;
  unsigned warpsAtBarrier = 0;
  std::vector<uint32_t> shared;
};

struct SMState {
  std::vector<WarpState> warps;
  std::vector<BlockState> blocks; ///< resident blocks
  unsigned lastIssued = 0;        ///< round-robin pointer
  bool issuedThisCycle = false;   ///< for visualisation
  uint64_t issued = 0;
  uint64_t idleSlots = 0;
};

class SimtGpu {
public:
  using ReadFn = std::function<uint32_t(uint32_t addr)>;
  using WriteFn = std::function<void(uint32_t addr, uint32_t value)>;

  SimtGpu();
  SimtGpu(const GpuConfig &config, ReadFn read, WriteFn write);

  void setMemoryCallbacks(ReadFn read, WriteFn write);
  /// Changes the configuration. Aborts any running kernel.
  void setConfig(const GpuConfig &config);
  const GpuConfig &config() const { return m_config; }

  /// Resets all state (aborts any running kernel, clears statistics).
  void reset();

  /**
   * @brief launch
   * Starts executing a kernel. Returns false (and sets errorMessage()) if the
   * parameters are invalid (e.g. a block larger than one SM can hold).
   * If a kernel is already running the call is ignored: it returns false and
   * leaves the running kernel (including its error state) untouched.
   */
  bool launch(const LaunchParams &params);

  /**
   * @brief fault
   * Reports a bus error from inside a memory callback (e.g. the kernel
   * accessed an address that the bus refuses to serve). The kernel stops with
   * errorMessage() = @p msg at the end of the current instruction; the value
   * returned by the callback is discarded.
   */
  void fault(const std::string &msg);

  /// Complete dynamic state of the model (everything except the
  /// configuration and the memory callbacks). Used for reverse execution.
  struct State {
    std::vector<SMState> sms;
    GpuStats stats;
    LaunchParams params;
    bool launched = false;
    bool busy = false;
    uint32_t nextBlock = 0;
    std::string error;
  };
  State saveState() const;
  /// Restores a state saved under the *same* configuration.
  void restoreState(const State &s);
  /// Approximate memory footprint of @p s in bytes (inline + heap).
  static size_t stateBytes(const State &s);

  /// Aborts the running kernel (all warps are discarded). done() becomes true.
  void abort();

  /// Advances every SM by one cycle. Does nothing if no kernel is running.
  void step();

  bool busy() const { return m_busy; }
  /// True when a kernel was launched and has finished (or was aborted).
  bool done() const { return m_launched && !m_busy; }
  bool hasError() const { return !m_error.empty(); }
  const std::string &errorMessage() const { return m_error; }

  const GpuStats &stats() const { return m_stats; }
  const LaunchParams &launchParams() const { return m_params; }
  const std::vector<SMState> &sms() const { return m_sms; }
  /// Blocks not yet dispatched to any SM.
  uint32_t pendingBlocks() const {
    return m_params.gridX > m_nextBlock ? m_params.gridX - m_nextBlock : 0;
  }
  uint32_t nextBlock() const { return m_nextBlock; }

  /// Mask with one bit per lane.
  uint32_t fullMask() const {
    return m_config.warpSize >= 32 ? 0xFFFFFFFFu
                                   : ((1u << m_config.warpSize) - 1u);
  }
  /// Number of warps needed for a block of @p blockX threads.
  unsigned warpsPerBlock(uint32_t blockX) const {
    return (blockX + m_config.warpSize - 1) / m_config.warpSize;
  }

private:
  void rebuildSMs();
  void tryDispatch(unsigned smIdx);
  void dispatchBlock(unsigned smIdx, uint32_t blockId);
  void retireFinished(unsigned smIdx);
  void execute(unsigned smIdx, unsigned warpIdx);
  void fail(const std::string &msg);
  BlockState *findBlock(SMState &sm, uint32_t blockId);
  static void normalizePaths(WarpState &w);
  /// Called once every live lane of @p w is parked at a barrier.
  void arriveAtBarrier(SMState &sm, WarpState &w);
  /// Releases every warp of block @p blockId that waits at the barrier.
  void releaseBarrier(SMState &sm, uint32_t blockId);

  uint32_t readReg(const WarpState &w, unsigned lane, unsigned r) const {
    return r == 0 ? 0u : w.regs[lane * 16 + r];
  }
  void writeReg(WarpState &w, unsigned lane, unsigned r, uint32_t v) {
    if (r != 0)
      w.regs[lane * 16 + r] = v;
  }
  uint32_t sreg(const WarpState &w, unsigned smIdx, unsigned lane,
                unsigned id) const;
  static bool evalCond(Cond c, uint32_t a, uint32_t b);
  static uint32_t alu(Opcode op, uint32_t a, uint32_t b, uint32_t c);
  static unsigned popcount(uint32_t v);

  GpuConfig m_config;
  ReadFn m_read;
  WriteFn m_write;
  std::vector<SMState> m_sms;
  GpuStats m_stats;
  LaunchParams m_params;
  bool m_launched = false;
  bool m_busy = false;
  uint32_t m_nextBlock = 0;
  std::string m_error;
  std::string m_pendingFault; ///< set by fault(), consumed by execute()
};

} // namespace gpu
} // namespace Ripes

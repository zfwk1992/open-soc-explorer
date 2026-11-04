#pragma once

/**
 * @file tensorunit.h
 * A cycle-stepped, Qt-free model of a small weight-stationary systolic-array
 * tensor accelerator ("NPU"), together with the host-facing control logic that
 * a CPU interacts with: a descriptor ring in host memory, a doorbell, a DMA
 * engine and an interrupt status register.
 *
 * The model is deliberately simple so that every cycle can be visualized:
 *
 *  - Array: N x N processing elements (PEs), default 4x4. Each PE holds a
 *    stationary int8 weight `w`, receives an int8 activation `a` from its left
 *    neighbour and a partial sum from its upper neighbour, computes
 *    acc = psum_in + a * w, and forwards `a` to the right and `acc` downwards.
 *    Activations are injected skewed (row k is delayed k cycles) so that the
 *    partial sums of one output element meet in the same column at the right
 *    time. Results leave the bottom row one column per cycle.
 *
 *  - Descriptors: 32-byte little-endian records in host memory, see
 *    `Descriptor`. The CPU fills the ring at QUEUE_BASE + idx*32, then writes
 *    TAIL (the doorbell). The unit consumes entries HEAD..TAIL-1 (mod
 *    QUEUE_SIZE) and advances HEAD as each descriptor completes.
 *
 *  - DMA: matrices are moved between host memory and the on-chip tile buffers
 *    `dmaWordsPerCycle` 32-bit words per cycle (int8 elements are packed 4
 *    per word). Moving data is therefore typically what dominates the cycle
 *    count for small matrices - which is the point of the exercise.
 *
 *  - Memory access is done through two std::function callbacks
 *    (`MemoryCallbacks`). `read(addr, bytes)` / `write(addr, value, bytes)`
 *    use byte addresses, `bytes` is 1 or 4, values are little-endian and
 *    zero-extended. This maps 1:1 onto Ripes' IOBase::memRead / memWrite.
 *
 * State machine of one descriptor:
 *
 *    IDLE -> FETCH_DESC -> [ DMA_IN -> COMPUTE ]* -> DMA_OUT -> ... -> COMPLETE
 *
 *  MATMUL tiles the M x K x N product onto the array: for each (m-tile,
 *  n-tile) the K dimension is swept in K-tiles; each K-tile needs a DMA_IN
 *  (A and B sub-tiles) and a COMPUTE (weight load + systolic pass); the int32
 *  accumulator tile is written back by DMA_OUT once all K-tiles are done.
 *  RELU / ADD stream the array-sized chunks of int32 elements through the
 *  array used as a plain SIMD unit (N lanes per cycle).
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Ripes {
namespace npu {

// ---------------------------------------------------------------------------
// Descriptor
// ---------------------------------------------------------------------------

/// Size of one descriptor in host memory, in bytes / 32-bit words.
constexpr uint32_t kDescriptorBytes = 32;
constexpr uint32_t kDescriptorWords = kDescriptorBytes / 4;

/// Opcodes stored in Descriptor::opcode.
enum class Opcode : uint32_t {
  NOP = 0,    ///< Completes immediately (useful for ring/IRQ tests).
  MATMUL = 1, ///< C[M][N] (int32) = A[M][K] (int8) x B[K][N] (int8)
  RELU = 2,   ///< dst[i] = max(0, srcA[i]), M*N int32 elements
  ADD = 3,    ///< dst[i] = srcA[i] + srcB[i], M*N int32 elements
};

/// Bits of Descriptor::flags.
namespace DescFlags {
constexpr uint32_t RELU_FUSED = 1u << 0; ///< MATMUL: apply ReLU before writing C
constexpr uint32_t NO_IRQ = 1u << 1;     ///< Do not raise DESC_DONE for this one
} // namespace DescFlags

/**
 * @brief Task descriptor. Exactly the 32-byte memory layout, all fields
 * little-endian uint32:
 *
 *   offset  field    meaning
 *   +0      opcode   Opcode
 *   +4      srcA     byte address of A (MATMUL: M x K int8 row-major;
 *                    RELU/ADD: M*N int32)
 *   +8      srcB     byte address of B (MATMUL: K x N int8 row-major;
 *                    ADD: M*N int32; otherwise ignored)
 *   +12     dst      byte address of the output (M x N int32 row-major)
 *   +16     M        rows of A / C
 *   +20     N        columns of B / C
 *   +24     K        columns of A == rows of B (MATMUL only)
 *   +28     flags    DescFlags
 */
struct Descriptor {
  uint32_t opcode = 0;
  uint32_t srcA = 0;
  uint32_t srcB = 0;
  uint32_t dst = 0;
  uint32_t M = 0;
  uint32_t N = 0;
  uint32_t K = 0;
  uint32_t flags = 0;

  uint32_t word(unsigned idx) const;
  void setWord(unsigned idx, uint32_t value);
};

// ---------------------------------------------------------------------------
// Register-level constants (shared by the model, the peripheral and the docs)
// ---------------------------------------------------------------------------

enum class State : uint32_t {
  IDLE = 0,
  FETCH_DESC = 1,
  DMA_IN = 2,
  COMPUTE = 3,
  DMA_OUT = 4,
  COMPLETE = 5,
};
const char *stateName(State s);
constexpr unsigned kNumStates = 6;

/// Why the last descriptor was rejected / aborted (STATUS.ERROR).
enum class ErrorCause : uint32_t {
  NONE = 0,
  BAD_OPCODE = 1,    ///< opcode is not one of Opcode
  BAD_DIMENSION = 2, ///< M, N or K outside [1, kMaxDim]
  BUS_FAULT = 3,     ///< a DMA access was refused (see reportBusError())
};
const char *errorCauseName(ErrorCause c);

/// STATUS register bits.
namespace Status {
constexpr uint32_t BUSY = 1u << 0;          ///< state != IDLE
constexpr uint32_t STATE_SHIFT = 4;         ///< bits [7:4] = State
constexpr uint32_t STATE_MASK = 0xFu << 4;
constexpr uint32_t QUEUE_EMPTY = 1u << 8;   ///< head == tail
constexpr uint32_t ERROR = 1u << 9;         ///< last descriptor was rejected
constexpr uint32_t WEIGHT_LOAD = 1u << 10;  ///< COMPUTE: loading weights
constexpr uint32_t DMA_FAULT = 1u << 11;    ///< ERROR was caused by a bus fault
} // namespace Status

/// IRQ_EN / IRQ_STATUS register bits (IRQ_STATUS is write-1-to-clear).
namespace Irq {
constexpr uint32_t DESC_DONE = 1u << 0;   ///< a descriptor completed
constexpr uint32_t QUEUE_EMPTY = 1u << 1; ///< the last pending descriptor completed
constexpr uint32_t ERROR = 1u << 2;       ///< a descriptor was rejected
constexpr uint32_t ALL = DESC_DONE | QUEUE_EMPTY | ERROR;
} // namespace Irq

/// CTRL register bits (write-only, self clearing).
namespace Ctrl {
constexpr uint32_t SOFT_RESET = 1u << 0;     ///< abort, head = tail = 0, irq = 0
constexpr uint32_t CLEAR_COUNTERS = 1u << 1; ///< zero all counters
} // namespace Ctrl

// ---------------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------------

/// Per-PE state, retained for visualization.
struct PEState {
  int8_t a = 0;        ///< activation currently in the PE
  int8_t w = 0;        ///< stationary weight
  int32_t acc = 0;     ///< partial sum produced this cycle (psum_in + a*w)
  bool aValid = false; ///< `a` carries a real activation (not padding)
  bool wValid = false; ///< `w` is a real weight (inside the K x N tile)
};

struct MemoryCallbacks {
  /// Read `bytes` (1 or 4) at byte address `addr`; little-endian,
  /// zero-extended.
  std::function<uint32_t(uint32_t addr, unsigned bytes)> read;
  /// Write the low `bytes` (1 or 4) bytes of `value` at byte address `addr`.
  std::function<void(uint32_t addr, uint32_t value, unsigned bytes)> write;
};

struct Counters {
  uint64_t cycles = 0;          ///< step() calls since reset / clear
  uint64_t busyCycles = 0;      ///< cycles in which state != IDLE
  uint64_t dmaBytes = 0;        ///< bytes moved by the DMA engine (both ways)
  uint64_t dmaBytesIn = 0;      ///< host -> unit bytes (descriptors + inputs)
  uint64_t dmaBytesOut = 0;     ///< unit -> host bytes (results)
  uint64_t dmaTransfers = 0;    ///< DMA jobs started (descriptor fetch, tile
                                ///< load, writeback)
  uint64_t macs = 0;            ///< useful int8 multiply-accumulates
  uint64_t descriptorsDone = 0; ///< completed descriptors (incl. rejected)
  uint64_t errors = 0;          ///< rejected / aborted descriptors
  /// Cycles spent in each State (index = static_cast<unsigned>(State)).
  std::array<uint64_t, kNumStates> stateCycles{};
};

/// One host-memory write performed by the DMA engine, with the value it
/// replaced and the value it wrote (for undoing the write when the
/// simulation is reversed: the write is only undone while memory still holds
/// newValue, see IOTensorUnit::processorWasReversed).
struct DmaWriteUndo {
  uint32_t addr = 0;
  uint32_t oldValue = 0;
  uint32_t newValue = 0;
  unsigned bytes = 0;
};

/// Progress of the DMA transfer currently in flight.
struct DmaProgress {
  uint32_t bytesDone = 0;
  uint32_t bytesTotal = 0;
  bool writing = false; ///< false = host -> unit, true = unit -> host
};

/// Position in the tile loop of the current MATMUL.
struct TileInfo {
  uint32_t mTile = 0, nTile = 0, kTile = 0;
  uint32_t mTiles = 0, nTiles = 0, kTiles = 0;
};

/// A consistent copy of everything a visualization needs.
struct Snapshot {
  unsigned arrayN = 0;
  State state = State::IDLE;
  bool weightLoading = false;
  uint32_t queueBase = 0, queueSize = 0, head = 0, tail = 0;
  uint32_t status = 0, irqEnable = 0, irqStatus = 0;
  Counters counters;
  DmaProgress dma;
  TileInfo tile;
  bool hasDescriptor = false;
  Descriptor descriptor;
  uint32_t computeCycle = 0; ///< cycle index within the current systolic pass
  uint32_t computeCyclesTotal = 0; ///< length of the current systolic pass
  uint32_t weightLoadCycle = 0;    ///< rows shifted in so far (weight load)
  uint32_t tileM = 0, tileN = 0, tileK = 0; ///< valid extent of the tile
  uint32_t vecTotal = 0, vecOffset = 0, vecChunk = 0; ///< RELU / ADD
  uint32_t dmaWordsPerCycle = 1;
  ErrorCause errorCause = ErrorCause::NONE;
  uint32_t faultAddr = 0;   ///< address of the refused DMA access
  bool everStarted = false; ///< a doorbell has been seen since reset
  std::vector<PEState> pes; ///< arrayN*arrayN, row-major [k*N + n]
};

/**
 * Complete saved state of a TensorUnit (everything except the configuration
 * and the callbacks), used for reverse execution. The large buffers live in a
 * shared, immutable block that consecutive saves reuse while the unit is idle,
 * so an idle cycle costs only the small register part.
 */
struct SavedState {
  struct Regs;
  struct Buffers;
  std::shared_ptr<const Regs> regs;
  std::shared_ptr<const Buffers> buffers;
  /// Approximate heap footprint of this save, in bytes. `includeShared`
  /// counts the buffer block even if it is shared with the previous save.
  size_t bytes(bool includeShared = true) const;
};

class TensorUnit {
public:
  static constexpr unsigned kMinArrayN = 1;
  static constexpr unsigned kMaxArrayN = 32;
  static constexpr unsigned kMaxDmaWordsPerCycle = 64;
  /// Upper bound on each of M, N, K to keep runaway descriptors bounded.
  static constexpr uint32_t kMaxDim = 4096;

  explicit TensorUnit(unsigned arrayN = 4, unsigned dmaWordsPerCycle = 1);

  void setMemory(MemoryCallbacks callbacks);
  /// Optional callback, invoked (synchronously, from step()) whenever a newly
  /// set IRQ_STATUS bit is enabled in IRQ_EN.
  void setInterruptCallback(std::function<void()> callback);

  /// Re-dimension the unit. Implies reset().
  void configure(unsigned arrayN, unsigned dmaWordsPerCycle);
  unsigned arrayN() const { return m_arrayN; }
  unsigned dmaWordsPerCycle() const { return m_dmaWordsPerCycle; }

  /// Full reset: registers, counters, state, buffers.
  void reset();
  /// CTRL.SOFT_RESET: abort the current descriptor, head = tail = 0, clear
  /// IRQ_STATUS; keeps QUEUE_BASE/QUEUE_SIZE/IRQ_EN and the counters.
  void softReset();
  void clearCounters();

  /// Advance the model by one (or n) cycle(s).
  void step();
  void step(unsigned n);
  /// Convenience for tests: step until IDLE with an empty queue, or until
  /// `maxCycles` have elapsed. Returns true if the unit became idle.
  bool runUntilIdle(uint64_t maxCycles = 1000000);

  // --- Register interface ---------------------------------------------------
  uint32_t queueBase() const { return m_queueBase; }
  void setQueueBase(uint32_t base) { m_queueBase = base; }
  uint32_t queueSize() const { return m_queueSize; }
  void setQueueSize(uint32_t entries);
  uint32_t head() const { return m_head; }
  uint32_t tail() const { return m_tail; }
  /// Doorbell: CPU publishes the index of the first *unused* slot.
  void writeTail(uint32_t tail);
  uint32_t status() const;
  uint32_t irqEnable() const { return m_irqEnable; }
  void setIrqEnable(uint32_t mask) { m_irqEnable = mask & Irq::ALL; }
  uint32_t irqStatus() const { return m_irqStatus; }
  /// Write-1-to-clear.
  void clearIrqStatus(uint32_t mask) { m_irqStatus &= ~(mask & Irq::ALL); }
  bool irqPending() const { return (m_irqStatus & m_irqEnable) != 0; }
  void writeCtrl(uint32_t value);

  // --- Bus faults -----------------------------------------------------------
  /// Called by a MemoryCallbacks implementation (from within step()) when the
  /// access at `addr` must be refused, e.g. because it targets the unit's own
  /// register block. The current descriptor is aborted with
  /// ErrorCause::BUS_FAULT, STATUS.ERROR|DMA_FAULT and IRQ_STATUS.ERROR.
  void reportBusError(uint32_t addr);
  ErrorCause errorCause() const { return m_errorCause; }
  uint32_t faultAddr() const { return m_faultAddr; }

  // --- Reverse execution support -------------------------------------------
  /// When enabled, every DMA write first reads the value it overwrites and
  /// records it; takeWriteLog() returns (and clears) the log of the writes
  /// since the last call.
  void setWriteUndoLogging(bool enabled) { m_logWrites = enabled; }
  std::vector<DmaWriteUndo> takeWriteLog();
  SavedState saveState();
  void restoreState(const SavedState &state);

  // --- Introspection --------------------------------------------------------
  State state() const { return m_state; }
  bool idle() const { return m_state == State::IDLE; }
  bool queueEmpty() const { return m_head == m_tail; }
  const Counters &counters() const { return m_counters; }
  const std::vector<PEState> &peStates() const { return m_pes; }
  const PEState &pe(unsigned row, unsigned col) const {
    return m_pes[row * m_arrayN + col];
  }
  bool everStarted() const { return m_everStarted; }
  bool hasCurrentDescriptor() const { return m_hasDesc; }
  const Descriptor &currentDescriptor() const { return m_desc; }
  DmaProgress dmaProgress() const;
  TileInfo tileInfo() const { return m_tile; }
  Snapshot snapshot() const;

private:
  // Memory helpers (safe when no callbacks are installed).
  uint32_t rd32(uint32_t addr) const;
  uint8_t rd8(uint32_t addr) const;
  void wr32(uint32_t addr, uint32_t value);

  void raiseIrq(uint32_t bits);
  void finishDescriptor(bool error);
  /// If a bus fault was reported, aborts the descriptor and returns true.
  bool handleBusFault();
  void setError(ErrorCause cause);

  // State handlers; each is one cycle.
  void stepIdle();
  void stepFetchDesc();
  void stepDmaIn();
  void stepCompute();
  void stepDmaOut();
  void stepComplete();

  // Descriptor-specific sequencing.
  ErrorCause validateDescriptor(const Descriptor &d) const;
  void beginDescriptor();
  void beginMatmulKTile();
  void beginMatmulWriteback();
  bool advanceMatmulTile(); ///< returns false when all tiles are written back
  void beginVectorChunk();
  void vectorCompute();
  void beginVectorWriteback();

  // DMA job description.
  enum class DmaKind : uint8_t { A_INT8, B_INT8, VEC_A, VEC_B, OUT_INT32 };
  struct DmaElem {
    uint32_t addr;
    uint32_t index; ///< index into the destination/source buffer
    DmaKind kind;
  };
  static unsigned dmaElemBytes(DmaKind kind);
  void queueDma(DmaKind kind, uint32_t addr, uint32_t index);
  void startDma(bool writing);
  bool dmaStep(); ///< returns true when the queued transfer is finished
  void clearPEs();

  // Configuration
  unsigned m_arrayN = 4;
  unsigned m_dmaWordsPerCycle = 1;
  MemoryCallbacks m_mem;
  std::function<void()> m_irqCallback;

  // Reverse-execution bookkeeping
  bool m_logWrites = false;
  std::vector<DmaWriteUndo> m_writeLog;
  bool m_buffersDirty = true; ///< buffers changed since the last saveState()
  std::shared_ptr<const SavedState::Buffers> m_savedBuffers;

  // Registers
  uint32_t m_queueBase = 0;
  uint32_t m_queueSize = 0;
  uint32_t m_head = 0;
  uint32_t m_tail = 0;
  uint32_t m_irqEnable = 0;
  uint32_t m_irqStatus = 0;
  bool m_errorFlag = false;
  ErrorCause m_errorCause = ErrorCause::NONE;
  uint32_t m_faultAddr = 0;
  bool m_busFault = false; ///< a fault was reported during the current step
  bool m_everStarted = false;

  // Sequencing
  State m_state = State::IDLE;
  Counters m_counters;
  bool m_hasDesc = false;
  Descriptor m_desc;
  uint32_t m_fetchCyclesLeft = 0;

  // DMA
  std::vector<DmaElem> m_dmaQueue;
  size_t m_dmaPos = 0;
  uint32_t m_dmaBytesDone = 0;
  uint32_t m_dmaBytesTotal = 0;
  bool m_dmaWriting = false;

  // MATMUL tiling
  TileInfo m_tile;
  uint32_t m_tileM = 0, m_tileN = 0, m_tileK = 0; ///< valid extent of tile
  std::vector<int8_t> m_bufA;   ///< N x N, [m*N + k]
  std::vector<int8_t> m_bufB;   ///< N x N, [k*N + n]
  std::vector<int32_t> m_accC;  ///< N x N, [m*N + n]
  std::vector<int32_t> m_out;   ///< writeback staging
  std::vector<PEState> m_pes;   ///< N x N, [k*N + n]
  bool m_weightLoading = false;
  uint32_t m_weightLoadCycle = 0;
  uint32_t m_computeCycle = 0;
  uint32_t m_computeCyclesTotal = 0;

  // RELU / ADD streaming
  std::vector<int32_t> m_vecA;
  std::vector<int32_t> m_vecB;
  uint32_t m_vecTotal = 0;  ///< total elements (M*N)
  uint32_t m_vecOffset = 0; ///< first element of the current chunk
  uint32_t m_vecChunk = 0;  ///< elements in the current chunk
  uint32_t m_vecCyclesLeft = 0;
};

} // namespace npu
} // namespace Ripes

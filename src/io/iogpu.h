#pragma once

#include <QMutex>
#include <QVariant>
#include <QWidget>

#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <thread>
#include <vector>

#include "accel/gpu/simtgpu.h"
#include "iobase.h"

namespace Ripes {

class IOGpuWidget;

/**
 * @brief The IOGpu class
 * A memory-mapped SIMT GPU built around the Qt-free gpu::SimtGpu cycle model
 * (see accel/gpu/simtgpu.h for the kernel ISA and the microarchitecture).
 *
 * The host (RISC-V) program places the kernel machine code in memory, fills in
 * the launch registers and writes CTRL.START. The GPU then advances a
 * configurable number of GPU cycles for every processor cycle
 * (ProcessorHandler::processorClocked), so a program that polls STATUS really
 * does overlap with the kernel's execution.
 *
 * Register map (offsets from the peripheral base, all 32-bit):
 *   0x00 CTRL        RW  bit0 START (write 1 to launch; reads as 0),
 *                        bit1 IRQ_EN, bit2 ABORT (write 1 to stop a kernel)
 *   0x04 STATUS      RW  bit0 BUSY, bit1 DONE (sticky), bit2 ERROR (sticky),
 *                        bit3 IRQ (= DONE & IRQ_EN), bit4 LAUNCH_IGNORED
 *                        (sticky: START was written while BUSY); write 1 to
 *                        bits 1/2/4 to clear them (W1C)
 *   0x08 KERNEL_PC   RW  byte address of the first kernel instruction
 *   0x0C GRID_X      RW  number of thread blocks
 *   0x10 BLOCK_X     RW  threads per block (must fit in one SM)
 *   0x14 ARG0        RW  kernel argument 0 (readable with S_ARG rd, 0)
 *   0x18 ARG1        RW
 *   0x1C ARG2        RW
 *   0x20 ARG3        RW
 *   0x24 CYCLES      R   GPU cycles spent by the last/current kernel
 *   0x28 STATS_INSTRS   R   warp instructions issued
 *   0x2C STATS_DIVERGE  R   divergent branches (warp split into two paths)
 *   0x30 STATS_MEMTXN   R   32-byte global memory transactions
 *   0x34 STATS_IDLE     R   idle issue slots (SM-cycles with nothing to issue)
 *   0x38 CONFIG      R   [7:0] warp size, [15:8] warps per SM, [23:16] SMs
 *
 * Threading: ioRead/ioWrite and the per-cycle step run on the processor
 * thread while the widget reads a View on the GUI thread; every access to the
 * model goes through m_mutex.
 *
 * Re-entrancy: while the model performs bus accesses (kernel fetch, LDG/STG)
 * the processor thread holds m_mutex. If such an access lands in the GPU's own
 * register window, the bus calls back into ioRead/ioWrite on the same thread.
 * That case is detected (m_busThread) *before* locking, the access is refused
 * and reported as a kernel fault (STATUS.ERROR) instead of deadlocking.
 *
 * Reverse execution: after every processor cycle a frame of the complete
 * peripheral state is pushed onto a bounded history (at most
 * RIPES_SETTING_REWINDSTACKSIZE + 1 frames and kHistoryBudgetBytes bytes;
 * cycles that change nothing share the previous frame). On
 * ProcessorHandler::processorReversed one frame is popped and restored. If
 * the history is exhausted the GPU is reset to its pre-launch state and the
 * widget shows a notice. Global-memory writes done by the kernel (STG) are
 * not undone (they are not part of the processor's rewind log either).
 */
class IOGpu : public IOBase {
  Q_OBJECT

  enum Parameters { NUM_SMS, WARPS_PER_SM, WARP_SIZE, MEM_LATENCY, STEPS };

public:
  IOGpu(QWidget *parent);
  ~IOGpu() { unregister(); }

  enum Registers {
    CTRL,
    STATUS,
    KERNEL_PC,
    GRID_X,
    BLOCK_X,
    ARG0,
    ARG1,
    ARG2,
    ARG3,
    CYCLES,
    STATS_INSTRS,
    STATS_DIVERGE,
    STATS_MEMTXN,
    STATS_IDLE,
    CONFIG,
    NREGISTERS
  };

  // Bit definitions (also exported as assembler symbols).
  static constexpr uint32_t CTRL_START = 1u << 0;
  static constexpr uint32_t CTRL_IRQ_EN = 1u << 1;
  static constexpr uint32_t CTRL_ABORT = 1u << 2;
  static constexpr uint32_t STATUS_BUSY = 1u << 0;
  static constexpr uint32_t STATUS_DONE = 1u << 1;
  static constexpr uint32_t STATUS_ERROR = 1u << 2;
  static constexpr uint32_t STATUS_IRQ = 1u << 3;
  static constexpr uint32_t STATUS_LAUNCH_IGNORED = 1u << 4;

  /// Upper bound on the memory used by the reverse-execution history.
  static constexpr size_t kHistoryBudgetBytes = 64u * 1024u * 1024u;
  /// Maximum number of kernel words captured for the disassembly view.
  static constexpr unsigned kMaxListingWords = 256;

  virtual unsigned byteSize() const override { return NREGISTERS * 4; }
  virtual QString description() const override;
  virtual QString baseName() const override { return "GPU"; }

  virtual const std::vector<RegDesc> &registers() const override {
    return m_regDescs;
  }
  virtual const std::vector<IOSymbol> *extraSymbols() const override {
    return &m_extraSymbols;
  }

  /**
   * Hardware read/write functions
   */
  virtual VInt ioRead(AInt offset, unsigned size) override;
  virtual void ioWrite(AInt offset, VInt value, unsigned size) override;

  virtual void reset() override;

  QSize minimumSizeHint() const override;
  QSize sizeHint() const override;

  /// Events shown as a notice in the widget.
  enum class Notice { None, LaunchIgnored, ReverseRestored, ReverseReset };

  /// Immutable copy of everything the widget displays (GUI thread).
  struct LaneRegs {
    std::array<uint32_t, 16> r{};
    uint8_t preds = 0;
  };
  struct WarpView {
    bool valid = false;
    uint32_t blockId = 0;
    uint32_t warpInBlock = 0;
    uint32_t pc = 0;
    uint32_t activeMask = 0;
    uint32_t parkedMask = 0;
    uint32_t exitedMask = 0;
    uint32_t validMask = 0;
    bool hasLastInstr = false;
    uint32_t lastInstr = 0;
    uint32_t lastPc = 0;
    uint32_t lastExecMask = 0;
    unsigned stallCycles = 0;
    bool atBarrier = false;
    std::vector<gpu::WarpPath> paths;
    std::vector<LaneRegs> lanes;
  };
  struct SMView {
    std::vector<WarpView> warps;
    unsigned residentBlocks = 0;
    bool issuedThisCycle = false;
    int issuedWarp = -1; ///< warp slot issued in the last GPU cycle, or -1
    uint64_t issued = 0;
    uint64_t idleSlots = 0;
  };
  struct View {
    gpu::GpuConfig config;
    gpu::GpuStats stats;
    std::vector<SMView> sms;
    std::array<uint32_t, NREGISTERS> regs{};
    bool launched = false; ///< a kernel was launched since reset
    bool busy = false;
    bool done = false;
    bool error = false;
    bool launchIgnored = false;
    bool aborted = false; ///< the last kernel was stopped by CTRL.ABORT
    uint64_t ignoredLaunches = 0;
    QString errorMessage;
    uint32_t gridX = 0;
    uint32_t blockX = 0;
    uint32_t pendingBlocks = 0;
    uint32_t kernelBase = 0;
    std::shared_ptr<const std::vector<uint32_t>> kernelWords;
    Notice notice = Notice::None;
    unsigned stepsPerCycle = 1;
    size_t historyFrames = 0;
    size_t historyBytes = 0;
    size_t lastFrameBytes = 0;
  };
  View view() const;
  /// Incremented on every state change; the widget repaints when it moves.
  uint64_t version() const { return m_version.load(std::memory_order_relaxed); }

  /// Human readable meaning of register @p reg (translated).
  static QString registerMeaning(unsigned reg);
  /// Decodes the bits/fields of @p value for register @p reg (translated).
  static QString decodeRegister(unsigned reg, uint32_t value);
  /// Register name (as exported to the assembler, never translated).
  static QString registerName(unsigned reg);

  /// Footprint of one reverse-execution frame of the current state (bytes).
  size_t currentFrameBytes() const;

protected:
  virtual void parameterChanged(unsigned) override;

private:
  /// Per-cycle hook (processor thread, direct connection).
  void processorWasClocked();
  /// processorReversed hook: restore the previous frame.
  void processorWasReversed();
  /// Applies the IOParam values to the model (aborts a running kernel).
  void applyParameters();
  void updateRegs();
  uint32_t regRead(unsigned reg) const; // requires m_mutex held
  void launch();                        // requires m_mutex held
  void captureListing();                // requires m_mutex held
  bool isReentrant() const {
    return m_busThread.load(std::memory_order_acquire) ==
           std::this_thread::get_id();
  }
  void markChanged() {
    m_changed = true;
    m_version.fetch_add(1, std::memory_order_relaxed);
  }

  /// Host-visible register state (everything except the model).
  struct HostRegs {
    uint32_t kernelPc = 0;
    uint32_t gridX = 1;
    uint32_t blockX = 1;
    std::array<uint32_t, 4> args = {{0, 0, 0, 0}};
    bool irqEn = false;
    bool doneFlag = false;
    bool errorFlag = false;
    bool ignoredFlag = false;
    bool aborted = false; ///< the last kernel was stopped by CTRL.ABORT
    uint64_t ignoredLaunches = 0;
    uint32_t listingBase = 0;
    std::shared_ptr<const std::vector<uint32_t>> listing;
  };
  struct Frame {
    gpu::SimtGpu::State model;
    HostRegs host;
    size_t bytes = 0;
  };
  // History helpers; require m_mutex held.
  void pushFrame();
  void clearHistory();
  void trimHistory();

  gpu::SimtGpu m_gpu;
  mutable QMutex m_mutex;
  HostRegs m_host;
  Notice m_notice = Notice::None;

  /// Thread currently performing bus accesses on behalf of the model while
  /// holding m_mutex (default-constructed id = nobody).
  std::atomic<std::thread::id> m_busThread{};

  /// "GPU cycles per CPU cycle", cached by applyParameters() so that the
  /// per-cycle hook neither looks up the parameter map nor races with the GUI.
  std::atomic<unsigned> m_stepsPerCycle{1};
  std::atomic<size_t> m_rewindLimit{100};
  std::atomic<uint64_t> m_version{0};
  bool m_changed = true; ///< state changed since the last history frame

  std::deque<std::shared_ptr<const Frame>> m_history;
  size_t m_historyBytes = 0;
  size_t m_lastFrameBytes = 0;

  IOGpuWidget *m_widget = nullptr;
  std::vector<RegDesc> m_regDescs;
  std::vector<IOSymbol> m_extraSymbols;
};

} // namespace Ripes

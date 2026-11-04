#pragma once

#include <QMutex>
#include <QThread>
#include <QVariant>
#include <QWidget>

#include <atomic>
#include <deque>
#include <vector>

#include "accel/npu/tensorunit.h"
#include "iobase.h"

QT_FORWARD_DECLARE_CLASS(QTimer)

namespace Ripes {

class IOTensorUnitView;

/**
 * @brief The IOTensorUnit class
 * A memory-mapped tensor accelerator ("NPU") built around the Qt-free
 * npu::TensorUnit cycle model: a weight-stationary int8 systolic array driven
 * by a descriptor ring in host memory, a doorbell register, a DMA engine and
 * an interrupt status register.
 *
 * The unit advances one cycle for every processor cycle
 * (ProcessorHandler::processorClocked), so a program that rings the doorbell
 * and then polls STATUS/HEAD really does overlap with the accelerator's work.
 *
 * Register map (offsets from the peripheral base, all 32-bit):
 *   0x00 QUEUE_BASE  RW  byte address of the descriptor ring in memory
 *   0x04 QUEUE_SIZE  RW  number of 32-byte descriptor slots in the ring
 *   0x08 HEAD        R   index of the slot the unit will consume next
 *   0x0C TAIL        RW  index of the first free slot; writing = doorbell
 *   0x10 STATUS      R   see npu::Status
 *   0x14 IRQ_EN      RW  see npu::Irq
 *   0x18 IRQ_STATUS  RW  see npu::Irq; write-1-to-clear
 *   0x1C CYCLES      R   cycles since reset (every CPU cycle, busy or not)
 *   0x20 DMA_BYTES   R   bytes moved by the DMA engine
 *   0x24 MACS        R   useful int8 multiply-accumulates performed
 *   0x28 ARRAY_N     R   systolic array dimension (N x N)
 *   0x2C BUSY_CYCLES R   cycles in which the unit was not IDLE
 *   0x30 CTRL        W   bit0 soft reset, bit1 clear counters
 *   0x34 FAULT_ADDR  R   address of the last refused DMA access
 *
 * Threading: ioRead/ioWrite and the per-cycle step are invoked on the
 * processor thread while the widget refreshes on the GUI thread, so every
 * access to the model goes through m_mutex and the GUI works on a Snapshot.
 *
 * Re-entrancy: the model's DMA accesses go through IOBase::memRead/memWrite,
 * i.e. through the processor's address space. If a descriptor (or
 * QUEUE_BASE) points into this peripheral's own register block, that access
 * comes back into ioRead/ioWrite on the same thread while step() still holds
 * m_mutex. Such re-entrant accesses are detected (m_stepThread) and refused
 * as a bus fault instead of deadlocking on the non-recursive mutex.
 *
 * Reverse execution: after every processor cycle the complete unit state is
 * pushed onto a bounded history (RIPES_SETTING_REWINDSTACKSIZE frames)
 * together with the old contents of every host-memory word the DMA engine
 * wrote in that cycle. ProcessorHandler::processorReversed pops one frame,
 * restores the previous state and undoes the DMA writes (only where memory
 * still holds what the DMA wrote: the processor rewinds its own stores
 * first, and a word the CPU and the DMA both wrote in one cycle must end up
 * at its pre-cycle value).
 */
class IOTensorUnit : public IOBase {
  Q_OBJECT

  enum Parameters { ARRAY_SIZE, DMA_WORDS, CELL_SIZE };

public:
  IOTensorUnit(QWidget *parent);
  ~IOTensorUnit() override;

  enum Registers {
    QUEUE_BASE,
    QUEUE_SIZE,
    HEAD,
    TAIL,
    STATUS,
    IRQ_EN,
    IRQ_STATUS,
    CYCLES,
    DMA_BYTES,
    MACS,
    ARRAY_N,
    BUSY_CYCLES,
    CTRL,
    FAULT_ADDR,
    NREGISTERS
  };

  /// What happened on the last reverse step (shown by the widget).
  enum class ReverseNotice { None, Rewound, Exhausted };

  /// Everything the widget shows, copied under the lock.
  struct ViewData {
    npu::Snapshot snap;
    size_t historyFrames = 0; ///< cycles that can currently be reversed
    size_t historyBytes = 0;  ///< approximate memory used by the history
    ReverseNotice reverseNotice = ReverseNotice::None;
    unsigned cellSize = 56;
  };

  virtual unsigned byteSize() const override { return NREGISTERS * 4; }
  virtual QString description() const override;
  virtual QString baseName() const override { return "Tensor Unit"; }

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

  /// Copy of the state for display (GUI thread; takes the lock briefly).
  ViewData viewData() const;
  /// Base address of this peripheral in the memory map (0 if unmapped).
  AInt baseAddress() const;
  /// Forces an immediate widget refresh, also when the widget is hidden
  /// (used by tests and screenshot tools).
  void refreshView();
  /// The widget tree (header, state machine, array, tabs).
  IOTensorUnitView *view() const { return m_view; }

protected:
  virtual void parameterChanged(unsigned) override;
  void showEvent(QShowEvent *event) override;

private:
  /// Slot for ProcessorHandler::processorClocked (direct connection; runs on
  /// the processor thread).
  void processorWasClocked();
  /// Slot for ProcessorHandler::processorReversed.
  void processorWasReversed();

  /// Reads a whole 32-bit register. Caller holds m_mutex.
  uint32_t regRead(unsigned reg) const;
  /// Writes a whole 32-bit register. Caller holds m_mutex.
  void regWrite(unsigned reg, uint32_t value);
  /// True if the calling thread is inside m_unit.step() (and so already
  /// holds m_mutex): the access is a DMA access to our own registers.
  bool isReentrantAccess() const {
    return m_stepThread.load(std::memory_order_acquire) ==
           QThread::currentThreadId();
  }

  void updateRegs();
  void applyParameters();
  /// Clears the reverse history and records the current state as its first
  /// frame. Caller holds m_mutex.
  void resetHistory();
  /// Pushes the current state as one history frame. Caller holds m_mutex.
  void pushHistory();
  void maybeRefresh();

  npu::TensorUnit m_unit;
  mutable QMutex m_mutex;
  std::atomic<Qt::HANDLE> m_stepThread{nullptr};
  /// Set by ioRead/ioWrite when a re-entrant access was refused; only
  /// touched on the stepping thread.
  bool m_reentryFault = false;

  // Parameter values cached for lock-free use outside the GUI thread
  // (the IOParam map is modified by the GUI thread).
  std::atomic<unsigned> m_arrayN{4};
  std::atomic<unsigned> m_dmaWords{1};
  std::atomic<unsigned> m_cellSize{56};
  std::atomic<unsigned> m_rewindLimit{100};

  // Reverse execution history (guarded by m_mutex).
  struct Frame {
    npu::SavedState state;
    std::vector<npu::DmaWriteUndo> writes; ///< DMA writes made in this cycle
  };
  std::deque<Frame> m_history;
  ReverseNotice m_reverseNotice = ReverseNotice::None;

  // Set whenever the model changed; a GUI timer turns it into a refresh at a
  // bounded rate (a per-cycle cross-thread update would dominate run time).
  std::atomic<bool> m_dirty{true};
  QTimer *m_refreshTimer = nullptr;
  IOTensorUnitView *m_view = nullptr;

  std::vector<RegDesc> m_regDescs;
  std::vector<IOSymbol> m_extraSymbols;
};

} // namespace Ripes

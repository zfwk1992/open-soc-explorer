#include "iotensorunit.h"

#include <QMutexLocker>
#include <QTimer>
#include <QVBoxLayout>

#include "iomanager.h"
#include "ioregistry.h"
#include "iotensorunitwidget.h"
#include "processorhandler.h"
#include "ripessettings.h"

namespace Ripes {

namespace {
// Interval of the GUI refresh timer. The processor thread only sets a dirty
// flag; the timer turns it into at most one refresh per interval.
constexpr int kRefreshIntervalMs = 50;
} // namespace

IOTensorUnit::IOTensorUnit(QWidget *parent)
    : IOBase(IOType::TENSOR_UNIT, parent) {
  // Parameters
  m_parameters[ARRAY_SIZE] =
      IOParam(ARRAY_SIZE, tr("Array size (N x N)"), 4, true, 1,
              static_cast<int>(npu::TensorUnit::kMaxArrayN));
  m_parameters[DMA_WORDS] =
      IOParam(DMA_WORDS, tr("DMA words/cycle"), 1, true, 1,
              static_cast<int>(npu::TensorUnit::kMaxDmaWordsPerCycle));
  m_parameters[CELL_SIZE] =
      IOParam(CELL_SIZE, tr("PE cell size (pixels)"), 56, true, 24, 160);

  // Bus access for the DMA engine. memRead/memWrite are installed by the
  // IOManager once the peripheral is placed in the memory map; guard against
  // them not being set yet. Accesses that come back into this peripheral
  // (re-entrant, see isReentrantAccess()) are refused by ioRead/ioWrite and
  // reported to the model as a bus fault.
  npu::MemoryCallbacks mem;
  mem.read = [this](uint32_t addr, unsigned bytes) -> uint32_t {
    if (!memRead)
      return 0;
    m_reentryFault = false;
    const auto v = static_cast<uint32_t>(memRead(addr, bytes));
    if (m_reentryFault) {
      m_unit.reportBusError(addr);
      return 0;
    }
    return v;
  };
  mem.write = [this](uint32_t addr, uint32_t value, unsigned bytes) {
    if (!memWrite)
      return;
    m_reentryFault = false;
    memWrite(addr, value, bytes);
    if (m_reentryFault)
      m_unit.reportBusError(addr);
  };
  m_unit.setMemory(mem);
  m_unit.setWriteUndoLogging(true);

  m_rewindLimit.store(static_cast<unsigned>(std::max(
      0, RipesSettings::value(RIPES_SETTING_REWINDSTACKSIZE).toInt())));
  connect(RipesSettings::getObserver(RIPES_SETTING_REWINDSTACKSIZE),
          &SettingObserver::modified, this, [this](const QVariant &v) {
            m_rewindLimit.store(static_cast<unsigned>(std::max(0, v.toInt())));
          });

  applyParameters();
  updateRegs();

  // Step the model in lockstep with the processor. Direct connection: the
  // handler runs on whichever thread clocks the processor, in cycle order.
  connect(ProcessorHandler::get(), &ProcessorHandler::processorClocked, this,
          &IOTensorUnit::processorWasClocked, Qt::DirectConnection);
  // Pre-reversal hook: runs before the processor rewinds its own stores, so
  // the DMA writes of the cycle are undone in true LIFO order.
  connect(ProcessorHandler::get(), &ProcessorHandler::processorReversing, this,
          &IOTensorUnit::processorWillReverse, Qt::DirectConnection);

  // The widget tree.
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  m_view = new IOTensorUnitView(this);
  layout->addWidget(m_view);

  // Refresh when single-stepping / after a run (also re-reads the ring from
  // memory, which is only safe while the processor is not running).
  connect(ProcessorHandler::get(), &ProcessorHandler::procStateChangedNonRun,
          this, [this] {
            m_dirty.store(true);
            maybeRefresh();
          });
  connect(ProcessorHandler::get(), &ProcessorHandler::runFinished, this,
          [this] {
            m_dirty.store(true);
            maybeRefresh();
          });
  connect(&IOManager::get(), &IOManager::memoryMapChanged, this, [this] {
    m_dirty.store(true);
    maybeRefresh();
  });

  m_refreshTimer = new QTimer(this);
  m_refreshTimer->setInterval(kRefreshIntervalMs);
  connect(m_refreshTimer, &QTimer::timeout, this, &IOTensorUnit::maybeRefresh);
  m_refreshTimer->start();
}

IOTensorUnit::~IOTensorUnit() {
  if (m_refreshTimer)
    m_refreshTimer->stop();
  unregister();
}

QString IOTensorUnit::description() const {
  QStringList desc;
  desc << tr("A weight-stationary int8 systolic-array tensor accelerator "
             "(NPU) driven by a descriptor ring in memory.");
  desc << "";
  desc << tr("Write 32-byte descriptors {opcode, srcA, srcB, dst, M, N, K, "
             "flags} (little-endian words) to QUEUE_BASE + idx*32, then write "
             "the index of the next free slot to TAIL (the doorbell). The unit "
             "consumes entries from HEAD up to TAIL, advancing HEAD as each "
             "one completes.");
  desc << "";
  desc << tr("Opcodes: 1 = MATMUL (C[M][N] int32 = A[M][K] int8 x B[K][N] "
             "int8), 2 = RELU (M*N int32, in place allowed), 3 = ADD (M*N "
             "int32), 0 = NOP. flags: bit0 fuse ReLU into MATMUL, bit1 "
             "suppress DESC_DONE.");
  desc << "";
  desc << tr("STATUS: bit0 busy, bits[7:4] state (0 idle, 1 fetch, 2 dma-in, "
             "3 compute, 4 dma-out, 5 complete), bit8 queue empty, bit9 "
             "error, bit10 weight load, bit11 DMA fault. IRQ_STATUS/IRQ_EN: "
             "bit0 descriptor done, bit1 queue empty, bit2 error; IRQ_STATUS "
             "is write-1-to-clear. CTRL: bit0 soft reset, bit1 clear "
             "counters. FAULT_ADDR: address of the last refused DMA access "
             "(e.g. a ring or matrix placed inside the unit's own registers).");
  desc << "";
  desc << tr("The unit advances one cycle per processor cycle; CYCLES "
             "therefore doubles as a free-running cycle counter for timing "
             "CPU code. Reverse execution rewinds the unit together with the "
             "processor.");
  return desc.join('\n');
}

void IOTensorUnit::updateRegs() {
  const auto n = static_cast<VInt>(m_arrayN.load());

  m_regDescs.clear();
  const auto add = [this](const QString &name, RegDesc::RW rw, unsigned reg) {
    m_regDescs.push_back(
        RegDesc{name, rw, 32, static_cast<AInt>(reg) * 4, /*exported=*/true});
  };
  add("QUEUE_BASE", RegDesc::RW::RW, QUEUE_BASE);
  add("QUEUE_SIZE", RegDesc::RW::RW, QUEUE_SIZE);
  add("HEAD", RegDesc::RW::R, HEAD);
  add("TAIL", RegDesc::RW::RW, TAIL);
  add("STATUS", RegDesc::RW::R, STATUS);
  add("IRQ_EN", RegDesc::RW::RW, IRQ_EN);
  add("IRQ_STATUS", RegDesc::RW::RW, IRQ_STATUS);
  add("CYCLES", RegDesc::RW::R, CYCLES);
  add("DMA_BYTES", RegDesc::RW::R, DMA_BYTES);
  add("MACS", RegDesc::RW::R, MACS);
  add("ARRAY_N", RegDesc::RW::R, ARRAY_N);
  add("BUSY_CYCLES", RegDesc::RW::R, BUSY_CYCLES);
  add("CTRL", RegDesc::RW::W, CTRL);
  add("FAULT_ADDR", RegDesc::RW::R, FAULT_ADDR);

  m_extraSymbols.clear();
  m_extraSymbols.push_back(IOSymbol{"N", n});
  m_extraSymbols.push_back(IOSymbol{"DESC_SIZE", npu::kDescriptorBytes});
  // Descriptor field byte offsets.
  m_extraSymbols.push_back(IOSymbol{"DESC_OPCODE", 0});
  m_extraSymbols.push_back(IOSymbol{"DESC_SRCA", 4});
  m_extraSymbols.push_back(IOSymbol{"DESC_SRCB", 8});
  m_extraSymbols.push_back(IOSymbol{"DESC_DST", 12});
  m_extraSymbols.push_back(IOSymbol{"DESC_M", 16});
  m_extraSymbols.push_back(IOSymbol{"DESC_N", 20});
  m_extraSymbols.push_back(IOSymbol{"DESC_K", 24});
  m_extraSymbols.push_back(IOSymbol{"DESC_FLAGS", 28});
  // Opcodes and flags.
  m_extraSymbols.push_back(
      IOSymbol{"OP_NOP", static_cast<VInt>(npu::Opcode::NOP)});
  m_extraSymbols.push_back(
      IOSymbol{"OP_MATMUL", static_cast<VInt>(npu::Opcode::MATMUL)});
  m_extraSymbols.push_back(
      IOSymbol{"OP_RELU", static_cast<VInt>(npu::Opcode::RELU)});
  m_extraSymbols.push_back(
      IOSymbol{"OP_ADD", static_cast<VInt>(npu::Opcode::ADD)});
  m_extraSymbols.push_back(
      IOSymbol{"FLAG_RELU_FUSED", npu::DescFlags::RELU_FUSED});
  m_extraSymbols.push_back(IOSymbol{"FLAG_NO_IRQ", npu::DescFlags::NO_IRQ});
  // Status / IRQ / CTRL bits.
  m_extraSymbols.push_back(IOSymbol{"STATUS_BUSY", npu::Status::BUSY});
  m_extraSymbols.push_back(
      IOSymbol{"STATUS_QUEUE_EMPTY", npu::Status::QUEUE_EMPTY});
  m_extraSymbols.push_back(IOSymbol{"STATUS_ERROR", npu::Status::ERROR});
  m_extraSymbols.push_back(
      IOSymbol{"STATUS_DMA_FAULT", npu::Status::DMA_FAULT});
  m_extraSymbols.push_back(IOSymbol{"IRQ_DESC_DONE", npu::Irq::DESC_DONE});
  m_extraSymbols.push_back(
      IOSymbol{"IRQ_QUEUE_EMPTY", npu::Irq::QUEUE_EMPTY});
  m_extraSymbols.push_back(IOSymbol{"IRQ_ERROR", npu::Irq::ERROR});
  m_extraSymbols.push_back(
      IOSymbol{"CTRL_SOFT_RESET", npu::Ctrl::SOFT_RESET});
  m_extraSymbols.push_back(
      IOSymbol{"CTRL_CLEAR_COUNTERS", npu::Ctrl::CLEAR_COUNTERS});

  updateGeometry();
  emit regMapChanged();
}

void IOTensorUnit::applyParameters() {
  // GUI thread: read the IOParam map once and publish the values as atomics.
  const unsigned n = m_parameters.at(ARRAY_SIZE).value.toUInt();
  const unsigned words = m_parameters.at(DMA_WORDS).value.toUInt();
  const unsigned cell = m_parameters.at(CELL_SIZE).value.toUInt();
  m_cellSize.store(cell);
  QMutexLocker locker(&m_mutex);
  if (n != m_unit.arrayN() || words != m_unit.dmaWordsPerCycle()) {
    m_unit.configure(n, words);
    resetHistory();
  }
  m_arrayN.store(m_unit.arrayN());
  m_dmaWords.store(m_unit.dmaWordsPerCycle());
}

void IOTensorUnit::parameterChanged(unsigned) {
  applyParameters();
  updateRegs();
  m_dirty.store(true);
  maybeRefresh();
}

void IOTensorUnit::reset() {
  {
    QMutexLocker locker(&m_mutex);
    m_unit.reset();
    m_reverseNotice = ReverseNotice::None;
    resetHistory();
  }
  m_dirty.store(true);
}

// ---------------------------------------------------------------------------
// Reverse-execution history (caller holds m_mutex)
// ---------------------------------------------------------------------------

void IOTensorUnit::resetHistory() {
  m_history.clear();
  m_unit.takeWriteLog();
  pushHistory();
}

void IOTensorUnit::pushHistory() {
  Frame f;
  f.state = m_unit.saveState();
  f.writes = m_unit.takeWriteLog();
  m_history.push_back(std::move(f));
  // Frames needed to reverse `limit` cycles: limit + 1 (the oldest frame is
  // the state to return to).
  const size_t limit = static_cast<size_t>(m_rewindLimit.load()) + 1;
  while (m_history.size() > limit)
    m_history.pop_front();
}

// ---------------------------------------------------------------------------
// Register access (processor thread)
// ---------------------------------------------------------------------------

uint32_t IOTensorUnit::regRead(unsigned reg) const {
  switch (reg) {
  case QUEUE_BASE:
    return m_unit.queueBase();
  case QUEUE_SIZE:
    return m_unit.queueSize();
  case HEAD:
    return m_unit.head();
  case TAIL:
    return m_unit.tail();
  case STATUS:
    return m_unit.status();
  case IRQ_EN:
    return m_unit.irqEnable();
  case IRQ_STATUS:
    return m_unit.irqStatus();
  case CYCLES:
    return static_cast<uint32_t>(m_unit.counters().cycles);
  case DMA_BYTES:
    return static_cast<uint32_t>(m_unit.counters().dmaBytes);
  case MACS:
    return static_cast<uint32_t>(m_unit.counters().macs);
  case ARRAY_N:
    return m_unit.arrayN();
  case BUSY_CYCLES:
    return static_cast<uint32_t>(m_unit.counters().busyCycles);
  case CTRL:
    return 0; // write-only
  case FAULT_ADDR:
    return m_unit.faultAddr();
  default:
    return 0;
  }
}

void IOTensorUnit::regWrite(unsigned reg, uint32_t value) {
  switch (reg) {
  case QUEUE_BASE:
    m_unit.setQueueBase(value);
    break;
  case QUEUE_SIZE:
    m_unit.setQueueSize(value);
    break;
  case TAIL:
    m_unit.writeTail(value); // doorbell
    break;
  case IRQ_EN:
    m_unit.setIrqEnable(value);
    break;
  case IRQ_STATUS:
    m_unit.clearIrqStatus(value); // W1C
    break;
  case CTRL:
    m_unit.writeCtrl(value);
    break;
  case HEAD:
  case STATUS:
  case CYCLES:
  case DMA_BYTES:
  case MACS:
  case ARRAY_N:
  case BUSY_CYCLES:
  case FAULT_ADDR:
  default:
    break; // read-only
  }
}

VInt IOTensorUnit::ioRead(AInt offset, unsigned size) {
  if (isReentrantAccess()) {
    // DMA read of our own registers from inside step(): refuse it (the
    // stepping thread already holds m_mutex).
    m_reentryFault = true;
    return 0;
  }
  const unsigned reg = static_cast<unsigned>(offset / 4);
  if (reg >= NREGISTERS)
    return 0;
  uint32_t value;
  {
    QMutexLocker locker(&m_mutex);
    value = regRead(reg);
  }
  return (value >> ((offset % 4) * 8)) & vsrtl::generateBitmask(size * 8);
}

void IOTensorUnit::ioWrite(AInt offset, VInt value, unsigned) {
  if (isReentrantAccess()) {
    m_reentryFault = true;
    return;
  }
  const unsigned reg = static_cast<unsigned>(offset / 4);
  if (reg >= NREGISTERS)
    return;
  {
    QMutexLocker locker(&m_mutex);
    regWrite(reg, static_cast<uint32_t>(value));
  }
  m_dirty.store(true, std::memory_order_relaxed);
}

void IOTensorUnit::processorWasClocked() {
  QMutexLocker locker(&m_mutex);
  m_stepThread.store(QThread::currentThreadId(), std::memory_order_release);
  m_unit.step();
  m_stepThread.store(nullptr, std::memory_order_release);
  pushHistory();
  m_reverseNotice = ReverseNotice::None;
  m_dirty.store(true, std::memory_order_relaxed);
}

void IOTensorUnit::processorWillReverse() {
  std::vector<npu::DmaWriteUndo> undo;
  {
    QMutexLocker locker(&m_mutex);
    if (m_history.size() >= 2) {
      undo = std::move(m_history.back().writes);
      m_history.pop_back();
      m_unit.restoreState(m_history.back().state);
      m_reverseNotice = ReverseNotice::Rewound;
    } else {
      // The processor went back further than the unit's history reaches
      // (e.g. the unit was created mid-program or the history limit is
      // smaller than the processor's). Keep the state and say so.
      m_reverseNotice = ReverseNotice::Exhausted;
    }
  }
  // Undo the host-memory writes of the reversed cycle, newest first. Done
  // without holding m_mutex (the accesses go through the address space).
  //
  // This runs before the processor rewinds its own stores of the cycle
  // (ProcessorHandler::processorReversing). The DMA engine steps after the
  // CPU within a cycle, so its writes are the newest and are undone first;
  // a word the CPU stored (v0 -> v1) and the DMA then overwrote (v1 -> v2)
  // goes back to v1 here and to v0 when the processor restores its store.
  // No read-back is needed, so a DMA target inside a peripheral window is
  // not touched by a bus read either.
  if (memWrite) {
    for (auto it = undo.rbegin(); it != undo.rend(); ++it)
      memWrite(it->addr, it->oldValue, it->bytes);
  }
  m_dirty.store(true);
}

// ---------------------------------------------------------------------------
// GUI side
// ---------------------------------------------------------------------------

IOTensorUnit::ViewData IOTensorUnit::viewData() const {
  ViewData d;
  QMutexLocker locker(&m_mutex);
  d.snap = m_unit.snapshot();
  d.historyFrames = m_history.empty() ? 0 : m_history.size() - 1;
  const npu::SavedState::Buffers *prev = nullptr;
  for (const auto &f : m_history) {
    // Count each shared buffer block once.
    const bool shared = f.state.buffers.get() == prev;
    d.historyBytes += f.state.bytes(!shared) +
                      f.writes.size() * sizeof(npu::DmaWriteUndo);
    prev = f.state.buffers.get();
  }
  d.reverseNotice = m_reverseNotice;
  d.cellSize = m_cellSize.load();
  return d;
}

AInt IOTensorUnit::baseAddress() const {
  for (const auto &kv : IOManager::get().memoryMap())
    if (kv.second.name == name())
      return kv.second.startAddr;
  return 0;
}

void IOTensorUnit::maybeRefresh() {
  if (!m_view || !isVisible())
    return;
  if (!m_dirty.exchange(false))
    return;
  m_view->refresh(viewData());
}

void IOTensorUnit::refreshView() {
  if (!m_view)
    return;
  m_dirty.store(false);
  m_view->refresh(viewData());
}

void IOTensorUnit::showEvent(QShowEvent *event) {
  IOBase::showEvent(event);
  m_dirty.store(true);
  maybeRefresh();
}

} // namespace Ripes

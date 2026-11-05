#include "iogpu.h"

#include <QMutexLocker>
#include <QVBoxLayout>

#include <sstream>

#include "iogpuwidget.h"
#include "ioregistry.h"
#include "processorhandler.h"
#include "ripessettings.h"

namespace Ripes {

IOGpu::IOGpu(QWidget *parent) : IOBase(IOType::GPU, parent) {
  // Parameters
  m_parameters[NUM_SMS] = IOParam(NUM_SMS, tr("SMs"), 2, true, 1, 8);
  m_parameters[WARPS_PER_SM] =
      IOParam(WARPS_PER_SM, tr("Warps per SM"), 4, true, 1, 16);
  m_parameters[WARP_SIZE] =
      IOParam(WARP_SIZE, tr("Warp size (lanes)"), 8, true, 1, 32);
  m_parameters[MEM_LATENCY] = IOParam(
      MEM_LATENCY, tr("Global memory latency (cycles)"), 4, true, 0, 64);
  m_parameters[STEPS] =
      IOParam(STEPS, tr("GPU cycles per CPU cycle"), 1, true, 1, 64);

  // Bus access of the model. memRead/memWrite are installed by the IOManager
  // after construction, hence the null checks. These run on the processor
  // thread while m_mutex is held and m_busThread marks this thread, so an
  // access that lands in our own register window is caught in ioRead/ioWrite.
  m_gpu.setMemoryCallbacks(
      [this](uint32_t addr) -> uint32_t {
        if (!memRead)
          return 0;
        return static_cast<uint32_t>(memRead(addr, 4));
      },
      [this](uint32_t addr, uint32_t value) {
        if (memWrite)
          memWrite(addr, value, 4);
      });

  if (RipesSettings::hasSetting(RIPES_SETTING_REWINDSTACKSIZE)) {
    m_rewindLimit.store(
        RipesSettings::value(RIPES_SETTING_REWINDSTACKSIZE).toUInt());
    connect(RipesSettings::getObserver(RIPES_SETTING_REWINDSTACKSIZE),
            &SettingObserver::modified, this, [this](const QVariant &v) {
              m_rewindLimit.store(v.toUInt());
              QMutexLocker locker(&m_mutex);
              trimHistory();
            });
  }

  applyParameters();
  updateRegs();
  {
    QMutexLocker locker(&m_mutex);
    clearHistory();
  }

  // Step the model in lockstep with the processor. Direct connection: the
  // handler runs on whichever thread clocks the processor, in cycle order.
  connect(ProcessorHandler::get(), &ProcessorHandler::processorClocked, this,
          &IOGpu::processorWasClocked, Qt::DirectConnection);
  connect(ProcessorHandler::get(), &ProcessorHandler::processorReversed, this,
          &IOGpu::processorWasReversed, Qt::DirectConnection);

  // The visualisation. It polls version() at the UI update rate, so the
  // processor thread never posts per-cycle events to the GUI thread.
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  m_widget = new IOGpuWidget(this, this);
  layout->addWidget(m_widget);
}

QString IOGpu::description() const {
  QStringList desc;
  desc << tr("A small SIMT GPU: a number of streaming multiprocessors (SMs), "
             "each holding several warps of lanes (threads) that execute one "
             "instruction in lockstep under an active mask.");
  desc << "";
  desc << tr("Place the kernel machine code (see docs/arch/gpu.md for the ISA "
             "and tools/gpu_asm.py for an assembler) in memory, write its "
             "address to KERNEL_PC, the number of blocks to GRID_X, the "
             "threads per block to BLOCK_X, up to four arguments to "
             "ARG0..ARG3, and write 1 (START) to CTRL. Poll STATUS until DONE "
             "(bit1) is set; write 1 to that bit to clear it. The GPU advances "
             "a configurable number of cycles for every processor cycle.");
  desc << "";
  desc << tr("STATUS: bit0 BUSY, bit1 DONE, bit2 ERROR (e.g. undefined "
             "instruction, a block larger than one SM can hold, or a kernel "
             "access to the GPU's own registers), bit3 IRQ (DONE and "
             "CTRL.IRQ_EN), bit4 LAUNCH_IGNORED (START written while BUSY). "
             "The CYCLES and STATS_* registers expose GPU cycles, issued warp "
             "instructions, divergent branches, 32-byte global memory "
             "transactions and idle issue slots of the last kernel.");
  desc << "";
  desc << tr("Widget: status strip, one row per warp slot of every SM (lane "
             "squares show the active mask), kernel disassembly with the "
             "current PCs, statistics and the register table. Press \"?\" "
             "for the tutorial.");
  return desc.join('\n');
}

QString IOGpu::registerName(unsigned reg) {
  switch (reg) {
  case CTRL:
    return "CTRL";
  case STATUS:
    return "STATUS";
  case KERNEL_PC:
    return "KERNEL_PC";
  case GRID_X:
    return "GRID_X";
  case BLOCK_X:
    return "BLOCK_X";
  case ARG0:
    return "ARG0";
  case ARG1:
    return "ARG1";
  case ARG2:
    return "ARG2";
  case ARG3:
    return "ARG3";
  case CYCLES:
    return "CYCLES";
  case STATS_INSTRS:
    return "STATS_INSTRS";
  case STATS_DIVERGE:
    return "STATS_DIVERGE";
  case STATS_MEMTXN:
    return "STATS_MEMTXN";
  case STATS_IDLE:
    return "STATS_IDLE";
  case CONFIG:
    return "CONFIG";
  default:
    return QString();
  }
}

QString IOGpu::registerMeaning(unsigned reg) {
  switch (reg) {
  case CTRL:
    return tr("Control: bit0 START (write 1 to launch), bit1 IRQ_EN, bit2 "
              "ABORT. Every write sets IRQ_EN from bit1, so writing START "
              "alone clears IRQ_EN (write START|IRQ_EN to keep it)");
  case STATUS:
    return tr("Status: bit0 BUSY, bit1 DONE, bit2 ERROR, bit3 IRQ, bit4 "
              "LAUNCH_IGNORED (bits 1/2/4: write 1 to clear)");
  case KERNEL_PC:
    return tr("Byte address of the first kernel instruction");
  case GRID_X:
    return tr("Number of thread blocks in the grid");
  case BLOCK_X:
    return tr("Threads per block (must fit in one SM)");
  case ARG0:
  case ARG1:
  case ARG2:
  case ARG3:
    return tr("Kernel argument %1 (read with S_ARG rd, %1)").arg(reg - ARG0);
  case CYCLES:
    return tr("GPU cycles of the last/current kernel");
  case STATS_INSTRS:
    return tr("Warp instructions issued");
  case STATS_DIVERGE:
    return tr("Divergent branches (a warp split into two paths)");
  case STATS_MEMTXN:
    return tr("32-byte global memory transactions");
  case STATS_IDLE:
    return tr("Idle issue slots (SM-cycles with no ready warp)");
  case CONFIG:
    return tr("Configuration: [7:0] warp size, [15:8] warps per SM, [23:16] "
              "SMs");
  default:
    return QString();
  }
}

QString IOGpu::decodeRegister(unsigned reg, uint32_t v) {
  auto bits = [v](std::initializer_list<std::pair<uint32_t, const char *>> l) {
    QStringList set;
    for (const auto &b : l)
      if (v & b.first)
        set << QString::fromLatin1(b.second);
    return set.isEmpty() ? QStringLiteral("-") : set.join(" | ");
  };
  switch (reg) {
  case CTRL:
    return bits({{CTRL_IRQ_EN, "IRQ_EN"}});
  case STATUS:
    return bits({{STATUS_BUSY, "BUSY"},
                 {STATUS_DONE, "DONE"},
                 {STATUS_ERROR, "ERROR"},
                 {STATUS_IRQ, "IRQ"},
                 {STATUS_LAUNCH_IGNORED, "LAUNCH_IGNORED"}});
  case CONFIG:
    return tr("%1 SM x %2 warps x %3 lanes")
        .arg((v >> 16) & 0xFF)
        .arg((v >> 8) & 0xFF)
        .arg(v & 0xFF);
  case KERNEL_PC:
    return QStringLiteral("0x%1").arg(v, 8, 16, QChar('0'));
  default:
    return QString::number(v);
  }
}

void IOGpu::updateRegs() {
  m_regDescs.clear();
  auto add = [&](RegDesc::RW rw, unsigned reg) {
    m_regDescs.push_back(RegDesc{registerName(reg), rw, 32, reg * 4, true});
  };
  for (unsigned r = CTRL; r <= ARG3; ++r)
    add(RegDesc::RW::RW, r);
  for (unsigned r = CYCLES; r < NREGISTERS; ++r)
    add(RegDesc::RW::R, r);

  const auto cfg = m_gpu.config();
  m_extraSymbols.clear();
  m_extraSymbols.push_back(IOSymbol{"CTRL_START", CTRL_START});
  m_extraSymbols.push_back(IOSymbol{"CTRL_IRQ_EN", CTRL_IRQ_EN});
  m_extraSymbols.push_back(IOSymbol{"CTRL_ABORT", CTRL_ABORT});
  m_extraSymbols.push_back(IOSymbol{"STATUS_BUSY", STATUS_BUSY});
  m_extraSymbols.push_back(IOSymbol{"STATUS_DONE", STATUS_DONE});
  m_extraSymbols.push_back(IOSymbol{"STATUS_ERROR", STATUS_ERROR});
  m_extraSymbols.push_back(IOSymbol{"STATUS_IRQ", STATUS_IRQ});
  m_extraSymbols.push_back(
      IOSymbol{"STATUS_LAUNCH_IGNORED", STATUS_LAUNCH_IGNORED});
  m_extraSymbols.push_back(IOSymbol{"NUM_SMS", cfg.numSMs});
  m_extraSymbols.push_back(IOSymbol{"WARPS_PER_SM", cfg.warpsPerSM});
  m_extraSymbols.push_back(IOSymbol{"WARP_SIZE", cfg.warpSize});

  updateGeometry();
  emit regMapChanged();
}

void IOGpu::applyParameters() {
  gpu::GpuConfig cfg;
  cfg.numSMs = m_parameters.at(NUM_SMS).value.toUInt();
  cfg.warpsPerSM = m_parameters.at(WARPS_PER_SM).value.toUInt();
  cfg.warpSize = m_parameters.at(WARP_SIZE).value.toUInt();
  cfg.memLatency = m_parameters.at(MEM_LATENCY).value.toUInt();
  m_stepsPerCycle.store(
      std::max(1u, m_parameters.at(STEPS).value.toUInt()),
      std::memory_order_relaxed);
  QMutexLocker locker(&m_mutex);
  const auto &cur = m_gpu.config();
  if (cfg.numSMs != cur.numSMs || cfg.warpsPerSM != cur.warpsPerSM ||
      cfg.warpSize != cur.warpSize || cfg.memLatency != cur.memLatency) {
    const bool wasBusy = m_gpu.busy();
    m_gpu.setConfig(cfg); // resets the model: the kernel (if any) is gone
    if (wasBusy) {
      // The parameters are editable while the processor is paused, i.e. in
      // the middle of a kernel. Silently dropping the kernel would leave
      // STATUS at neither BUSY nor DONE and a host poll loop would never
      // end: report it like CTRL.ABORT plus an error.
      m_host.doneFlag = true;
      m_host.errorFlag = true;
      m_host.aborted = true;
      m_host.hostError = tr("kernel aborted: GPU configuration changed");
      m_notice = Notice::ConfigChangedWhileBusy;
    } else {
      m_host.doneFlag = false;
      m_host.errorFlag = false;
      m_host.hostError.clear();
      // A notice about the previous kernel/history no longer applies either.
      m_notice = Notice::None;
    }
    m_host.ignoredFlag = false;
    // Frames of another configuration cannot be restored.
    clearHistory();
  }
  markChanged();
}

void IOGpu::parameterChanged(unsigned) {
  applyParameters();
  updateRegs();
  emit scheduleUpdate();
}

// Small enough for the I/O tab at 1280x800: the widget stacks its panels
// when narrow (see IOGpuWidget::resizeEvent).
QSize IOGpu::minimumSizeHint() const { return QSize(380, 400); }
QSize IOGpu::sizeHint() const { return QSize(820, 640); }

void IOGpu::reset() {
  {
    QMutexLocker locker(&m_mutex);
    m_gpu.reset();
    m_host = HostRegs();
    m_notice = Notice::None;
    clearHistory();
    markChanged();
  }
  emit scheduleUpdate();
}

// ---------------------------------------------------------------------------
// Reverse-execution history (all helpers require m_mutex)
// ---------------------------------------------------------------------------

void IOGpu::clearHistory() {
  m_history.clear();
  m_historyBytes = 0;
  m_changed = true;
  pushFrame();
}

void IOGpu::pushFrame() {
  if (!m_changed && !m_history.empty()) {
    // Nothing changed this cycle: share the previous frame (8 bytes).
    m_history.push_back(m_history.back());
  } else {
    auto frame = std::make_shared<Frame>();
    frame->model = m_gpu.saveState();
    frame->host = m_host;
    frame->bytes = gpu::SimtGpu::stateBytes(frame->model) + sizeof(Frame);
    m_lastFrameBytes = frame->bytes;
    m_historyBytes += frame->bytes;
    m_history.push_back(std::move(frame));
    m_changed = false;
  }
  trimHistory();
}

void IOGpu::trimHistory() {
  // One frame per rewindable processor cycle plus the current state.
  const size_t maxFrames = m_rewindLimit.load() + 1;
  while (m_history.size() > 1 &&
         (m_history.size() > maxFrames ||
          m_historyBytes > kHistoryBudgetBytes)) {
    const auto &oldest = m_history.front();
    // Only release the bytes when the last reference goes away.
    if (oldest.use_count() == 1)
      m_historyBytes -= oldest->bytes;
    m_history.pop_front();
  }
}

size_t IOGpu::currentFrameBytes() const {
  QMutexLocker locker(&m_mutex);
  return gpu::SimtGpu::stateBytes(m_gpu.saveState()) + sizeof(Frame);
}

void IOGpu::processorWasReversed() {
  {
    QMutexLocker locker(&m_mutex);
    if (m_history.size() >= 2) {
      const auto popped = m_history.back();
      m_history.pop_back();
      if (popped.use_count() == 1)
        m_historyBytes -= popped->bytes;
      const auto &top = m_history.back();
      m_gpu.restoreState(top->model);
      m_host = top->host;
      m_notice = Notice::ReverseRestored;
    } else {
      const bool pristine = !m_gpu.busy() && !m_gpu.done() &&
                            !m_host.doneFlag && !m_host.errorFlag;
      if (!pristine) {
        // Reversed past the recorded history: fall back to the pre-launch
        // state (launch registers are kept, the kernel and flags are gone).
        m_gpu.reset();
        m_host.doneFlag = false;
        m_host.errorFlag = false;
        m_host.ignoredFlag = false;
        m_host.aborted = false;
        m_host.hostError.clear();
        m_notice = Notice::ReverseReset;
        clearHistory();
      }
    }
    m_changed = false;
    m_version.fetch_add(1, std::memory_order_relaxed);
  }
  emit scheduleUpdate();
}

// ---------------------------------------------------------------------------
// Processor-thread side
// ---------------------------------------------------------------------------

void IOGpu::processorWasClocked() {
  const unsigned steps = m_stepsPerCycle.load(std::memory_order_relaxed);
  QMutexLocker locker(&m_mutex);
  if (m_gpu.busy()) {
    m_busThread.store(std::this_thread::get_id(), std::memory_order_release);
    for (unsigned i = 0; i < steps && m_gpu.busy(); ++i)
      m_gpu.step();
    m_busThread.store(std::thread::id(), std::memory_order_release);
    if (!m_gpu.busy()) {
      m_host.doneFlag = true;
      if (m_gpu.hasError())
        m_host.errorFlag = true;
    }
    markChanged();
  }
  pushFrame();
}

void IOGpu::captureListing() {
  // Snapshot of the kernel code for the disassembly view: read words until a
  // word that is not a canonical instruction encoding (typically the data
  // after the kernel: small integers decode as NOP with junk in the unused
  // fields) or the cap, then drop trailing zero words (padding).
  //
  // The scan must not have side effects: it does not go through the bus
  // (memRead) but peeks at the processor memory directly, and it stops at
  // the first address that belongs to a memory-mapped peripheral. A bus read
  // of a peripheral can change its state (a D-pad read clears the captured
  // press, an RTC read latches the time, a read of this GPU's own window is
  // refused as a kernel fault, ...), and the words after a kernel may well
  // be a peripheral.
  auto words = std::make_shared<std::vector<uint32_t>>();
  const auto &mem = ProcessorHandler::getMemory();
  for (unsigned i = 0; i < kMaxListingWords; ++i) {
    const AInt addr = m_host.kernelPc + i * 4u;
    if (mem.regionType(addr) == vsrtl::core::AddressSpace::RegionType::IO)
      break;
    const uint32_t w = static_cast<uint32_t>(mem.readMemConst(addr, 4));
    if (!gpu::decode(w).valid)
      break;
    // Canonical: re-assembling the disassembly gives back the same word.
    std::string err;
    const auto again = gpu::assembleKernel(gpu::disassemble(w), &err);
    if (again.size() != 1 || again[0] != w)
      break;
    words->push_back(w);
  }
  while (!words->empty() && words->back() == 0u)
    words->pop_back();
  m_host.listingBase = m_host.kernelPc;
  m_host.listing = std::move(words);
}

void IOGpu::launch() {
  gpu::LaunchParams params;
  params.kernelPc = m_host.kernelPc;
  params.gridX = m_host.gridX;
  params.blockX = m_host.blockX;
  params.args = m_host.args;
  m_host.doneFlag = false;
  m_host.errorFlag = false;
  m_host.aborted = false;
  m_host.hostError.clear();
  m_notice = Notice::None;
  captureListing();
  if (!m_gpu.launch(params)) {
    m_host.errorFlag = true;
    m_host.doneFlag = true;
  }
  markChanged();
}

uint32_t IOGpu::regRead(unsigned reg) const {
  const auto &st = m_gpu.stats();
  const auto &cfg = m_gpu.config();
  switch (reg) {
  case CTRL:
    return m_host.irqEn ? CTRL_IRQ_EN : 0u;
  case STATUS: {
    uint32_t v = 0;
    if (m_gpu.busy())
      v |= STATUS_BUSY;
    if (m_host.doneFlag)
      v |= STATUS_DONE;
    if (m_host.errorFlag)
      v |= STATUS_ERROR;
    if (m_host.doneFlag && m_host.irqEn)
      v |= STATUS_IRQ;
    if (m_host.ignoredFlag)
      v |= STATUS_LAUNCH_IGNORED;
    return v;
  }
  case KERNEL_PC:
    return m_host.kernelPc;
  case GRID_X:
    return m_host.gridX;
  case BLOCK_X:
    return m_host.blockX;
  case ARG0:
  case ARG1:
  case ARG2:
  case ARG3:
    return m_host.args[reg - ARG0];
  case CYCLES:
    return static_cast<uint32_t>(st.cycles);
  case STATS_INSTRS:
    return static_cast<uint32_t>(st.issued);
  case STATS_DIVERGE:
    return static_cast<uint32_t>(st.divergences);
  case STATS_MEMTXN:
    return static_cast<uint32_t>(st.memTransactions);
  case STATS_IDLE:
    return static_cast<uint32_t>(st.idleSlots);
  case CONFIG:
    return (cfg.warpSize & 0xFFu) | ((cfg.warpsPerSM & 0xFFu) << 8) |
           ((cfg.numSMs & 0xFFu) << 16);
  default:
    return 0;
  }
}

VInt IOGpu::ioRead(AInt offset, unsigned size) {
  if (isReentrant()) {
    // A kernel load (or instruction fetch) hit the GPU's own register window
    // while the model holds m_mutex on this thread. Refuse instead of
    // deadlocking; the model reports the fault as STATUS.ERROR.
    std::ostringstream os;
    os << "kernel read from the GPU's own register window (offset 0x"
       << std::hex << offset << ")";
    m_gpu.fault(os.str());
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
  return (static_cast<VInt>(value) >> ((offset % 4) * 8)) &
         vsrtl::generateBitmask(size * 8);
}

void IOGpu::ioWrite(AInt offset, VInt value, unsigned) {
  if (isReentrant()) {
    std::ostringstream os;
    os << "kernel write to the GPU's own register window (offset 0x"
       << std::hex << offset << ")";
    m_gpu.fault(os.str());
    return;
  }
  const unsigned reg = static_cast<unsigned>(offset / 4);
  if (reg >= NREGISTERS)
    return;
  const uint32_t v = static_cast<uint32_t>(value);
  {
    QMutexLocker locker(&m_mutex);
    switch (reg) {
    case CTRL:
      m_host.irqEn = (v & CTRL_IRQ_EN) != 0;
      if (v & CTRL_ABORT) {
        if (m_gpu.busy())
          m_host.aborted = true;
        m_gpu.abort();
        m_host.doneFlag = true;
      }
      if (v & CTRL_START) {
        if (m_gpu.busy()) {
          // Doorbell while BUSY: ignore it, the running kernel is untouched.
          m_host.ignoredFlag = true;
          m_host.ignoredLaunches++;
          m_notice = Notice::LaunchIgnored;
        } else {
          launch();
        }
      }
      break;
    case STATUS:
      if (v & STATUS_DONE)
        m_host.doneFlag = false;
      if (v & STATUS_ERROR)
        m_host.errorFlag = false;
      if (v & STATUS_LAUNCH_IGNORED)
        m_host.ignoredFlag = false;
      break;
    case KERNEL_PC:
      m_host.kernelPc = v;
      break;
    case GRID_X:
      m_host.gridX = v;
      break;
    case BLOCK_X:
      m_host.blockX = v;
      break;
    case ARG0:
    case ARG1:
    case ARG2:
    case ARG3:
      m_host.args[reg - ARG0] = v;
      break;
    default:
      // Read-only registers.
      break;
    }
    markChanged();
  }
  emit scheduleUpdate();
}

// ---------------------------------------------------------------------------
// GUI side
// ---------------------------------------------------------------------------

IOGpu::View IOGpu::view() const {
  View s;
  QMutexLocker locker(&m_mutex);
  s.config = m_gpu.config();
  s.stats = m_gpu.stats();
  for (unsigned r = 0; r < NREGISTERS; ++r)
    s.regs[r] = regRead(r);
  s.busy = m_gpu.busy();
  s.launched = m_gpu.busy() || m_gpu.done();
  s.done = m_host.doneFlag;
  s.error = m_host.errorFlag;
  s.launchIgnored = m_host.ignoredFlag;
  s.aborted = m_host.aborted;
  s.ignoredLaunches = m_host.ignoredLaunches;
  s.errorMessage = m_gpu.hasError()
                       ? QString::fromStdString(m_gpu.errorMessage())
                       : m_host.hostError;
  s.gridX = m_gpu.launchParams().gridX;
  s.blockX = m_gpu.launchParams().blockX;
  s.pendingBlocks = m_gpu.pendingBlocks();
  s.kernelBase = m_host.listingBase;
  s.kernelWords = m_host.listing;
  s.notice = m_notice;
  s.stepsPerCycle = m_stepsPerCycle.load(std::memory_order_relaxed);
  s.historyFrames = m_history.size();
  s.historyBytes = m_historyBytes;
  s.lastFrameBytes = m_lastFrameBytes;
  const unsigned ws = s.config.warpSize;
  for (const auto &sm : m_gpu.sms()) {
    SMView ss;
    ss.residentBlocks = static_cast<unsigned>(sm.blocks.size());
    ss.issuedThisCycle = sm.issuedThisCycle;
    ss.issuedWarp =
        sm.issuedThisCycle ? static_cast<int>(sm.lastIssued) : -1;
    ss.issued = sm.issued;
    ss.idleSlots = sm.idleSlots;
    for (const auto &w : sm.warps) {
      WarpView wv;
      wv.valid = w.valid;
      wv.blockId = w.blockId;
      wv.warpInBlock = w.warpInBlock;
      wv.pc = w.pc();
      wv.activeMask = w.activeMask();
      wv.parkedMask = w.parkedMask();
      wv.exitedMask = w.exitedMask;
      wv.validMask = w.validMask;
      wv.hasLastInstr = w.hasLastInstr;
      wv.lastInstr = w.lastInstr;
      wv.lastPc = w.lastPc;
      wv.lastExecMask = w.lastExecMask;
      wv.stallCycles = w.stallCycles;
      wv.atBarrier = w.atBarrier;
      wv.paths = w.paths;
      if (w.valid && w.regs.size() >= ws * 16u) {
        wv.lanes.resize(ws);
        for (unsigned lane = 0; lane < ws; ++lane) {
          for (unsigned r = 0; r < 16; ++r)
            wv.lanes[lane].r[r] = w.regs[lane * 16 + r];
          wv.lanes[lane].preds = lane < w.preds.size() ? w.preds[lane] : 0;
        }
      }
      ss.warps.push_back(std::move(wv));
    }
    s.sms.push_back(std::move(ss));
  }
  return s;
}

} // namespace Ripes

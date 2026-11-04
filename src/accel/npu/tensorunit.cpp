#include "tensorunit.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace Ripes {
namespace npu {

// ---------------------------------------------------------------------------
// Descriptor
// ---------------------------------------------------------------------------

uint32_t Descriptor::word(unsigned idx) const {
  switch (idx) {
  case 0:
    return opcode;
  case 1:
    return srcA;
  case 2:
    return srcB;
  case 3:
    return dst;
  case 4:
    return M;
  case 5:
    return N;
  case 6:
    return K;
  case 7:
    return flags;
  default:
    return 0;
  }
}

void Descriptor::setWord(unsigned idx, uint32_t value) {
  switch (idx) {
  case 0:
    opcode = value;
    break;
  case 1:
    srcA = value;
    break;
  case 2:
    srcB = value;
    break;
  case 3:
    dst = value;
    break;
  case 4:
    M = value;
    break;
  case 5:
    N = value;
    break;
  case 6:
    K = value;
    break;
  case 7:
    flags = value;
    break;
  default:
    break;
  }
}

const char *stateName(State s) {
  switch (s) {
  case State::IDLE:
    return "IDLE";
  case State::FETCH_DESC:
    return "FETCH_DESC";
  case State::DMA_IN:
    return "DMA_IN";
  case State::COMPUTE:
    return "COMPUTE";
  case State::DMA_OUT:
    return "DMA_OUT";
  case State::COMPLETE:
    return "COMPLETE";
  }
  return "?";
}

const char *errorCauseName(ErrorCause c) {
  switch (c) {
  case ErrorCause::NONE:
    return "NONE";
  case ErrorCause::BAD_OPCODE:
    return "BAD_OPCODE";
  case ErrorCause::BAD_DIMENSION:
    return "BAD_DIMENSION";
  case ErrorCause::BUS_FAULT:
    return "BUS_FAULT";
  }
  return "?";
}

// ---------------------------------------------------------------------------
// Saved state (reverse execution)
// ---------------------------------------------------------------------------

/// Small, always-copied part: registers, counters and sequencing.
struct SavedState::Regs {
  uint32_t queueBase, queueSize, head, tail, irqEnable, irqStatus;
  bool errorFlag;
  ErrorCause errorCause;
  uint32_t faultAddr;
  bool everStarted;
  State state;
  Counters counters;
  bool hasDesc;
  Descriptor desc;
  uint32_t fetchCyclesLeft;
  size_t dmaPos;
  uint32_t dmaBytesDone, dmaBytesTotal;
  bool dmaWriting;
  TileInfo tile;
  uint32_t tileM, tileN, tileK;
  bool weightLoading;
  uint32_t weightLoadCycle, computeCycle, computeCyclesTotal;
  uint32_t vecTotal, vecOffset, vecChunk, vecCyclesLeft;
};

/// Large part: on-chip buffers, PE array and the queued DMA job. Only
/// re-captured after a cycle in which the unit was busy.
struct SavedState::Buffers {
  std::vector<uint8_t> dmaQueue; ///< opaque copy of TensorUnit::m_dmaQueue
  std::vector<int8_t> bufA, bufB;
  std::vector<int32_t> accC, out, vecA, vecB;
  std::vector<PEState> pes;
};

size_t SavedState::bytes(bool includeShared) const {
  size_t n = 0;
  if (regs)
    n += sizeof(Regs);
  if (buffers && includeShared) {
    const auto &b = *buffers;
    n += sizeof(Buffers) + b.dmaQueue.size() + b.bufA.size() + b.bufB.size() +
         4 * (b.accC.size() + b.out.size() + b.vecA.size() + b.vecB.size()) +
         sizeof(PEState) * b.pes.size();
  }
  return n;
}

// ---------------------------------------------------------------------------
// Construction / configuration
// ---------------------------------------------------------------------------

TensorUnit::TensorUnit(unsigned arrayN, unsigned dmaWordsPerCycle) {
  configure(arrayN, dmaWordsPerCycle);
}

void TensorUnit::setMemory(MemoryCallbacks callbacks) {
  m_mem = std::move(callbacks);
}

void TensorUnit::setInterruptCallback(std::function<void()> callback) {
  m_irqCallback = std::move(callback);
}

void TensorUnit::configure(unsigned arrayN, unsigned dmaWordsPerCycle) {
  m_arrayN = std::clamp(arrayN, kMinArrayN, kMaxArrayN);
  m_dmaWordsPerCycle = std::clamp(dmaWordsPerCycle, 1u, kMaxDmaWordsPerCycle);
  const size_t cells = static_cast<size_t>(m_arrayN) * m_arrayN;
  m_bufA.assign(cells, 0);
  m_bufB.assign(cells, 0);
  m_accC.assign(cells, 0);
  m_out.assign(cells, 0);
  m_vecA.assign(cells, 0);
  m_vecB.assign(cells, 0);
  m_pes.assign(cells, PEState());
  reset();
}

void TensorUnit::reset() {
  m_queueBase = 0;
  m_queueSize = 0;
  m_head = 0;
  m_tail = 0;
  m_irqEnable = 0;
  m_irqStatus = 0;
  m_errorFlag = false;
  m_counters = Counters();
  m_everStarted = false;
  m_writeLog.clear();
  softReset();
}

void TensorUnit::softReset() {
  m_state = State::IDLE;
  m_head = 0;
  m_tail = 0;
  m_irqStatus = 0;
  m_errorFlag = false;
  m_errorCause = ErrorCause::NONE;
  m_faultAddr = 0;
  m_busFault = false;
  m_hasDesc = false;
  m_desc = Descriptor();
  m_fetchCyclesLeft = 0;
  m_dmaQueue.clear();
  m_dmaPos = 0;
  m_dmaBytesDone = 0;
  m_dmaBytesTotal = 0;
  m_dmaWriting = false;
  m_tile = TileInfo();
  m_tileM = m_tileN = m_tileK = 0;
  std::fill(m_bufA.begin(), m_bufA.end(), 0);
  std::fill(m_bufB.begin(), m_bufB.end(), 0);
  std::fill(m_accC.begin(), m_accC.end(), 0);
  std::fill(m_out.begin(), m_out.end(), 0);
  std::fill(m_vecA.begin(), m_vecA.end(), 0);
  std::fill(m_vecB.begin(), m_vecB.end(), 0);
  std::fill(m_pes.begin(), m_pes.end(), PEState());
  m_weightLoading = false;
  m_weightLoadCycle = 0;
  m_computeCycle = 0;
  m_computeCyclesTotal = 0;
  m_vecTotal = m_vecOffset = m_vecChunk = 0;
  m_vecCyclesLeft = 0;
  m_buffersDirty = true;
}

void TensorUnit::clearCounters() { m_counters = Counters(); }

// ---------------------------------------------------------------------------
// Register interface
// ---------------------------------------------------------------------------

void TensorUnit::setQueueSize(uint32_t entries) {
  m_queueSize = entries;
  if (m_queueSize == 0) {
    m_head = 0;
    m_tail = 0;
  } else {
    m_head %= m_queueSize;
    m_tail %= m_queueSize;
  }
}

void TensorUnit::writeTail(uint32_t tail) {
  m_tail = (m_queueSize == 0) ? 0 : (tail % m_queueSize);
  if (m_tail != m_head)
    m_everStarted = true;
}

uint32_t TensorUnit::status() const {
  uint32_t s = 0;
  if (m_state != State::IDLE)
    s |= Status::BUSY;
  s |= (static_cast<uint32_t>(m_state) << Status::STATE_SHIFT) &
       Status::STATE_MASK;
  if (queueEmpty())
    s |= Status::QUEUE_EMPTY;
  if (m_errorFlag)
    s |= Status::ERROR;
  if (m_state == State::COMPUTE && m_weightLoading)
    s |= Status::WEIGHT_LOAD;
  if (m_errorFlag && m_errorCause == ErrorCause::BUS_FAULT)
    s |= Status::DMA_FAULT;
  return s;
}

void TensorUnit::writeCtrl(uint32_t value) {
  if (value & Ctrl::SOFT_RESET)
    softReset();
  if (value & Ctrl::CLEAR_COUNTERS)
    clearCounters();
}

DmaProgress TensorUnit::dmaProgress() const {
  DmaProgress p;
  p.bytesDone = m_dmaBytesDone;
  p.bytesTotal = m_dmaBytesTotal;
  p.writing = m_dmaWriting;
  return p;
}

Snapshot TensorUnit::snapshot() const {
  Snapshot s;
  s.arrayN = m_arrayN;
  s.state = m_state;
  s.weightLoading = (m_state == State::COMPUTE) && m_weightLoading;
  s.queueBase = m_queueBase;
  s.queueSize = m_queueSize;
  s.head = m_head;
  s.tail = m_tail;
  s.status = status();
  s.irqEnable = m_irqEnable;
  s.irqStatus = m_irqStatus;
  s.counters = m_counters;
  s.dma = dmaProgress();
  s.tile = m_tile;
  s.hasDescriptor = m_hasDesc;
  s.descriptor = m_desc;
  s.computeCycle = m_computeCycle;
  s.computeCyclesTotal = m_computeCyclesTotal;
  s.weightLoadCycle = m_weightLoadCycle;
  s.tileM = m_tileM;
  s.tileN = m_tileN;
  s.tileK = m_tileK;
  s.vecTotal = m_vecTotal;
  s.vecOffset = m_vecOffset;
  s.vecChunk = m_vecChunk;
  s.dmaWordsPerCycle = m_dmaWordsPerCycle;
  s.errorCause = m_errorCause;
  s.faultAddr = m_faultAddr;
  s.everStarted = m_everStarted;
  s.pes = m_pes;
  return s;
}

// ---------------------------------------------------------------------------
// Bus faults and reverse execution
// ---------------------------------------------------------------------------

void TensorUnit::reportBusError(uint32_t addr) {
  if (!m_busFault) {
    m_busFault = true;
    m_faultAddr = addr;
  }
}

void TensorUnit::setError(ErrorCause cause) {
  m_errorFlag = true;
  m_errorCause = cause;
  m_counters.errors++;
}

bool TensorUnit::handleBusFault() {
  if (!m_busFault)
    return false;
  setError(ErrorCause::BUS_FAULT);
  m_dmaQueue.clear();
  m_dmaPos = 0;
  m_weightLoading = false;
  m_state = State::COMPLETE;
  return true;
}

std::vector<DmaWriteUndo> TensorUnit::takeWriteLog() {
  std::vector<DmaWriteUndo> log;
  log.swap(m_writeLog);
  return log;
}

SavedState TensorUnit::saveState() {
  SavedState st;
  auto r = std::make_shared<SavedState::Regs>();
  r->queueBase = m_queueBase;
  r->queueSize = m_queueSize;
  r->head = m_head;
  r->tail = m_tail;
  r->irqEnable = m_irqEnable;
  r->irqStatus = m_irqStatus;
  r->errorFlag = m_errorFlag;
  r->errorCause = m_errorCause;
  r->faultAddr = m_faultAddr;
  r->everStarted = m_everStarted;
  r->state = m_state;
  r->counters = m_counters;
  r->hasDesc = m_hasDesc;
  r->desc = m_desc;
  r->fetchCyclesLeft = m_fetchCyclesLeft;
  r->dmaPos = m_dmaPos;
  r->dmaBytesDone = m_dmaBytesDone;
  r->dmaBytesTotal = m_dmaBytesTotal;
  r->dmaWriting = m_dmaWriting;
  r->tile = m_tile;
  r->tileM = m_tileM;
  r->tileN = m_tileN;
  r->tileK = m_tileK;
  r->weightLoading = m_weightLoading;
  r->weightLoadCycle = m_weightLoadCycle;
  r->computeCycle = m_computeCycle;
  r->computeCyclesTotal = m_computeCyclesTotal;
  r->vecTotal = m_vecTotal;
  r->vecOffset = m_vecOffset;
  r->vecChunk = m_vecChunk;
  r->vecCyclesLeft = m_vecCyclesLeft;
  st.regs = std::move(r);

  if (m_buffersDirty || !m_savedBuffers) {
    auto b = std::make_shared<SavedState::Buffers>();
    b->dmaQueue.resize(m_dmaQueue.size() * sizeof(DmaElem));
    if (!m_dmaQueue.empty())
      std::memcpy(b->dmaQueue.data(), m_dmaQueue.data(), b->dmaQueue.size());
    b->bufA = m_bufA;
    b->bufB = m_bufB;
    b->accC = m_accC;
    b->out = m_out;
    b->vecA = m_vecA;
    b->vecB = m_vecB;
    b->pes = m_pes;
    m_savedBuffers = std::move(b);
    m_buffersDirty = false;
  }
  st.buffers = m_savedBuffers;
  return st;
}

void TensorUnit::restoreState(const SavedState &st) {
  if (!st.regs || !st.buffers)
    return;
  const auto &b = *st.buffers;
  // A save taken with a different array size cannot be applied.
  if (b.pes.size() != m_pes.size())
    return;
  const auto &r = *st.regs;
  m_queueBase = r.queueBase;
  m_queueSize = r.queueSize;
  m_head = r.head;
  m_tail = r.tail;
  m_irqEnable = r.irqEnable;
  m_irqStatus = r.irqStatus;
  m_errorFlag = r.errorFlag;
  m_errorCause = r.errorCause;
  m_faultAddr = r.faultAddr;
  m_busFault = false;
  m_everStarted = r.everStarted;
  m_state = r.state;
  m_counters = r.counters;
  m_hasDesc = r.hasDesc;
  m_desc = r.desc;
  m_fetchCyclesLeft = r.fetchCyclesLeft;
  m_dmaPos = r.dmaPos;
  m_dmaBytesDone = r.dmaBytesDone;
  m_dmaBytesTotal = r.dmaBytesTotal;
  m_dmaWriting = r.dmaWriting;
  m_tile = r.tile;
  m_tileM = r.tileM;
  m_tileN = r.tileN;
  m_tileK = r.tileK;
  m_weightLoading = r.weightLoading;
  m_weightLoadCycle = r.weightLoadCycle;
  m_computeCycle = r.computeCycle;
  m_computeCyclesTotal = r.computeCyclesTotal;
  m_vecTotal = r.vecTotal;
  m_vecOffset = r.vecOffset;
  m_vecChunk = r.vecChunk;
  m_vecCyclesLeft = r.vecCyclesLeft;

  m_dmaQueue.resize(b.dmaQueue.size() / sizeof(DmaElem));
  if (!m_dmaQueue.empty())
    std::memcpy(m_dmaQueue.data(), b.dmaQueue.data(), b.dmaQueue.size());
  m_bufA = b.bufA;
  m_bufB = b.bufB;
  m_accC = b.accC;
  m_out = b.out;
  m_vecA = b.vecA;
  m_vecB = b.vecB;
  m_pes = b.pes;
  // The live buffers now equal the saved block: share it again.
  m_savedBuffers = st.buffers;
  m_buffersDirty = false;
  m_writeLog.clear();
}

// ---------------------------------------------------------------------------
// Memory helpers
// ---------------------------------------------------------------------------

uint32_t TensorUnit::rd32(uint32_t addr) const {
  // After a refused access the rest of the transfer is not performed.
  if (m_busFault || !m_mem.read)
    return 0u;
  return m_mem.read(addr, 4);
}

uint8_t TensorUnit::rd8(uint32_t addr) const {
  if (m_busFault || !m_mem.read)
    return 0u;
  return static_cast<uint8_t>(m_mem.read(addr, 1) & 0xFFu);
}

void TensorUnit::wr32(uint32_t addr, uint32_t value) {
  if (m_busFault || !m_mem.write)
    return;
  if (m_logWrites && m_mem.read) {
    const uint32_t old = m_mem.read(addr, 4);
    if (m_busFault)
      return; // the address is not accessible at all
    m_writeLog.push_back(DmaWriteUndo{addr, old, value, 4});
  }
  m_mem.write(addr, value, 4);
}

void TensorUnit::raiseIrq(uint32_t bits) {
  bits &= Irq::ALL;
  m_irqStatus |= bits;
  if ((bits & m_irqEnable) && m_irqCallback)
    m_irqCallback();
}

// ---------------------------------------------------------------------------
// Stepping
// ---------------------------------------------------------------------------

void TensorUnit::step(unsigned n) {
  for (unsigned i = 0; i < n; ++i)
    step();
}

bool TensorUnit::runUntilIdle(uint64_t maxCycles) {
  uint64_t n = 0;
  while (!(idle() && queueEmpty())) {
    if (n++ >= maxCycles)
      return false;
    step();
  }
  return true;
}

void TensorUnit::step() {
  m_counters.cycles++;
  m_counters.stateCycles[static_cast<unsigned>(m_state)]++;
  if (m_state != State::IDLE) {
    m_counters.busyCycles++;
    // Any busy cycle may touch the buffers / PE array.
    m_buffersDirty = true;
  }

  switch (m_state) {
  case State::IDLE:
    stepIdle();
    break;
  case State::FETCH_DESC:
    stepFetchDesc();
    break;
  case State::DMA_IN:
    stepDmaIn();
    break;
  case State::COMPUTE:
    stepCompute();
    break;
  case State::DMA_OUT:
    stepDmaOut();
    break;
  case State::COMPLETE:
    stepComplete();
    break;
  }
}

void TensorUnit::stepIdle() {
  if (m_queueSize == 0 || queueEmpty())
    return;
  // Doorbell has been rung: start fetching the descriptor at HEAD. The fetch
  // of 8 words costs the same bandwidth as any other DMA transfer.
  m_state = State::FETCH_DESC;
  m_hasDesc = false;
  m_everStarted = true;
  m_counters.dmaTransfers++;
  m_fetchCyclesLeft =
      (kDescriptorWords + m_dmaWordsPerCycle - 1) / m_dmaWordsPerCycle;
}

void TensorUnit::stepFetchDesc() {
  if (m_queueSize == 0) {
    // QUEUE_SIZE was zeroed underneath us; drop back to idle.
    m_hasDesc = false;
    m_state = State::IDLE;
    return;
  }
  if (!m_hasDesc) {
    const uint32_t addr = m_queueBase + (m_head % m_queueSize) * kDescriptorBytes;
    // A new descriptor starts with a clean error state.
    m_errorFlag = false;
    m_errorCause = ErrorCause::NONE;
    m_faultAddr = 0;
    m_busFault = false;
    for (unsigned i = 0; i < kDescriptorWords; ++i)
      m_desc.setWord(i, rd32(addr + i * 4));
    m_hasDesc = true;
    if (handleBusFault())
      return;
    m_counters.dmaBytes += kDescriptorBytes;
    m_counters.dmaBytesIn += kDescriptorBytes;
  }
  if (m_fetchCyclesLeft > 0)
    m_fetchCyclesLeft--;
  if (m_fetchCyclesLeft != 0)
    return;

  const ErrorCause cause = validateDescriptor(m_desc);
  if (cause != ErrorCause::NONE) {
    setError(cause);
    m_state = State::COMPLETE;
    return;
  }
  beginDescriptor();
}

void TensorUnit::stepDmaIn() {
  const bool done = dmaStep();
  if (handleBusFault() || !done)
    return;

  const auto op = static_cast<Opcode>(m_desc.opcode);
  if (op == Opcode::MATMUL) {
    // Start the systolic pass: first shift the weights in, then stream A.
    clearPEs();
    m_weightLoading = true;
    m_weightLoadCycle = 0;
    m_computeCycle = 0;
    // Last output leaves the array at t = (tileM-1) + (N-1) + (N-1).
    m_computeCyclesTotal = m_tileM + 2 * m_arrayN - 2;
  } else {
    vectorCompute();
    m_vecCyclesLeft = (m_vecChunk + m_arrayN - 1) / m_arrayN;
  }
  m_state = State::COMPUTE;
}

void TensorUnit::stepCompute() {
  const auto op = static_cast<Opcode>(m_desc.opcode);
  const unsigned N = m_arrayN;

  if (op != Opcode::MATMUL) {
    if (m_vecCyclesLeft > 0)
      m_vecCyclesLeft--;
    if (m_vecCyclesLeft == 0)
      beginVectorWriteback();
    return;
  }

  if (m_weightLoading) {
    // Weights enter at the top row and shift downwards, one row per cycle.
    // Row r of the B tile is injected at cycle (N-1-r) so that after N cycles
    // PE(k, n) holds B[k][n].
    for (unsigned k = N - 1; k >= 1; --k) {
      for (unsigned n = 0; n < N; ++n) {
        m_pes[k * N + n].w = m_pes[(k - 1) * N + n].w;
        m_pes[k * N + n].wValid = m_pes[(k - 1) * N + n].wValid;
      }
    }
    const unsigned srcRow = N - 1 - m_weightLoadCycle;
    for (unsigned n = 0; n < N; ++n) {
      m_pes[n].w = m_bufB[srcRow * N + n];
      m_pes[n].wValid = (srcRow < m_tileK) && (n < m_tileN);
    }
    m_weightLoadCycle++;
    if (m_weightLoadCycle == N) {
      m_weightLoading = false;
      m_computeCycle = 0;
    }
    return;
  }

  const uint32_t t = m_computeCycle;

  // 1) Activations move one PE to the right; new (skewed) values enter at the
  //    left edge: row k receives A[t-k][k].
  for (unsigned k = 0; k < N; ++k) {
    for (unsigned n = N - 1; n >= 1; --n) {
      m_pes[k * N + n].a = m_pes[k * N + n - 1].a;
      m_pes[k * N + n].aValid = m_pes[k * N + n - 1].aValid;
    }
    PEState &edge = m_pes[k * N];
    const int64_t m = static_cast<int64_t>(t) - static_cast<int64_t>(k);
    if (m >= 0 && m < static_cast<int64_t>(m_tileM)) {
      edge.a = m_bufA[static_cast<size_t>(m) * N + k];
      edge.aValid = (k < m_tileK);
    } else {
      edge.a = 0;
      edge.aValid = false;
    }
  }

  // 2) Partial sums flow downwards. Rows are evaluated bottom-up so that each
  //    PE sees its upper neighbour's value from the *previous* cycle.
  for (unsigned kk = N; kk-- > 0;) {
    for (unsigned n = 0; n < N; ++n) {
      PEState &pe = m_pes[kk * N + n];
      const int32_t psumIn = (kk > 0) ? m_pes[(kk - 1) * N + n].acc : 0;
      pe.acc = psumIn + static_cast<int32_t>(pe.a) * static_cast<int32_t>(pe.w);
      if (pe.aValid && pe.wValid)
        m_counters.macs++;
    }
  }

  // 3) Results leave the bottom row: column n emits C[t-(N-1)-n][n].
  for (unsigned n = 0; n < N; ++n) {
    const int64_t m = static_cast<int64_t>(t) - static_cast<int64_t>(N - 1) -
                      static_cast<int64_t>(n);
    if (m >= 0 && m < static_cast<int64_t>(m_tileM))
      m_accC[static_cast<size_t>(m) * N + n] += m_pes[(N - 1) * N + n].acc;
  }

  m_computeCycle++;
  if (m_computeCycle < m_computeCyclesTotal)
    return;

  // K-tile finished.
  m_tile.kTile++;
  if (m_tile.kTile < m_tile.kTiles) {
    beginMatmulKTile();
  } else {
    beginMatmulWriteback();
  }
}

void TensorUnit::stepDmaOut() {
  const bool done = dmaStep();
  if (handleBusFault() || !done)
    return;

  const auto op = static_cast<Opcode>(m_desc.opcode);
  if (op == Opcode::MATMUL) {
    if (!advanceMatmulTile())
      m_state = State::COMPLETE;
    return;
  }

  m_vecOffset += m_vecChunk;
  if (m_vecOffset < m_vecTotal) {
    beginVectorChunk();
  } else {
    m_state = State::COMPLETE;
  }
}

void TensorUnit::stepComplete() { finishDescriptor(m_errorFlag); }

void TensorUnit::finishDescriptor(bool error) {
  if (m_queueSize != 0)
    m_head = (m_head + 1) % m_queueSize;
  m_counters.descriptorsDone++;
  m_hasDesc = false;
  m_state = State::IDLE;

  uint32_t irq = 0;
  if (error) {
    irq |= Irq::ERROR;
  } else if (!(m_desc.flags & DescFlags::NO_IRQ)) {
    irq |= Irq::DESC_DONE;
  }
  if (queueEmpty())
    irq |= Irq::QUEUE_EMPTY;
  raiseIrq(irq);
}

// ---------------------------------------------------------------------------
// Descriptor sequencing
// ---------------------------------------------------------------------------

ErrorCause TensorUnit::validateDescriptor(const Descriptor &d) const {
  const auto inRange = [](uint32_t v) { return v >= 1 && v <= kMaxDim; };
  const auto dims = [](bool ok) {
    return ok ? ErrorCause::NONE : ErrorCause::BAD_DIMENSION;
  };
  switch (static_cast<Opcode>(d.opcode)) {
  case Opcode::NOP:
    return ErrorCause::NONE;
  case Opcode::MATMUL:
    return dims(inRange(d.M) && inRange(d.N) && inRange(d.K));
  case Opcode::RELU:
  case Opcode::ADD:
    return dims(inRange(d.M) && inRange(d.N));
  }
  return ErrorCause::BAD_OPCODE;
}

void TensorUnit::beginDescriptor() {
  switch (static_cast<Opcode>(m_desc.opcode)) {
  case Opcode::NOP:
    m_state = State::COMPLETE;
    return;
  case Opcode::MATMUL: {
    const uint32_t N = m_arrayN;
    m_tile = TileInfo();
    m_tile.mTiles = (m_desc.M + N - 1) / N;
    m_tile.nTiles = (m_desc.N + N - 1) / N;
    m_tile.kTiles = (m_desc.K + N - 1) / N;
    std::fill(m_accC.begin(), m_accC.end(), 0);
    beginMatmulKTile();
    return;
  }
  case Opcode::RELU:
  case Opcode::ADD:
    m_vecTotal = m_desc.M * m_desc.N;
    m_vecOffset = 0;
    beginVectorChunk();
    return;
  }
  // Unreachable: validateDescriptor() rejects unknown opcodes.
  setError(ErrorCause::BAD_OPCODE);
  m_state = State::COMPLETE;
}

void TensorUnit::beginMatmulKTile() {
  const uint32_t N = m_arrayN;
  const uint32_t m0 = m_tile.mTile * N;
  const uint32_t n0 = m_tile.nTile * N;
  const uint32_t k0 = m_tile.kTile * N;
  m_tileM = std::min<uint32_t>(N, m_desc.M - m0);
  m_tileN = std::min<uint32_t>(N, m_desc.N - n0);
  m_tileK = std::min<uint32_t>(N, m_desc.K - k0);

  std::fill(m_bufA.begin(), m_bufA.end(), 0);
  std::fill(m_bufB.begin(), m_bufB.end(), 0);
  m_dmaQueue.clear();

  // A sub-tile: rows m0.., columns k0.. (row-major, K bytes per row).
  for (uint32_t m = 0; m < m_tileM; ++m)
    for (uint32_t k = 0; k < m_tileK; ++k)
      queueDma(DmaKind::A_INT8, m_desc.srcA + (m0 + m) * m_desc.K + (k0 + k),
               m * N + k);
  // B sub-tile: rows k0.., columns n0.. (row-major, N bytes per row).
  for (uint32_t k = 0; k < m_tileK; ++k)
    for (uint32_t n = 0; n < m_tileN; ++n)
      queueDma(DmaKind::B_INT8, m_desc.srcB + (k0 + k) * m_desc.N + (n0 + n),
               k * N + n);

  startDma(/*writing=*/false);
  m_state = State::DMA_IN;
}

void TensorUnit::beginMatmulWriteback() {
  const uint32_t N = m_arrayN;
  const uint32_t m0 = m_tile.mTile * N;
  const uint32_t n0 = m_tile.nTile * N;
  const bool relu = (m_desc.flags & DescFlags::RELU_FUSED) != 0;

  m_dmaQueue.clear();
  for (uint32_t m = 0; m < m_tileM; ++m) {
    for (uint32_t n = 0; n < m_tileN; ++n) {
      int32_t v = m_accC[m * N + n];
      if (relu && v < 0)
        v = 0;
      m_out[m * N + n] = v;
      queueDma(DmaKind::OUT_INT32,
               m_desc.dst + ((m0 + m) * m_desc.N + (n0 + n)) * 4, m * N + n);
    }
  }
  startDma(/*writing=*/true);
  m_state = State::DMA_OUT;
}

bool TensorUnit::advanceMatmulTile() {
  m_tile.nTile++;
  if (m_tile.nTile >= m_tile.nTiles) {
    m_tile.nTile = 0;
    m_tile.mTile++;
  }
  if (m_tile.mTile >= m_tile.mTiles)
    return false;
  m_tile.kTile = 0;
  std::fill(m_accC.begin(), m_accC.end(), 0);
  beginMatmulKTile();
  return true;
}

void TensorUnit::beginVectorChunk() {
  const uint32_t cells = m_arrayN * m_arrayN;
  m_vecChunk = std::min<uint32_t>(cells, m_vecTotal - m_vecOffset);
  const bool isAdd = static_cast<Opcode>(m_desc.opcode) == Opcode::ADD;

  m_dmaQueue.clear();
  for (uint32_t i = 0; i < m_vecChunk; ++i)
    queueDma(DmaKind::VEC_A, m_desc.srcA + (m_vecOffset + i) * 4, i);
  if (isAdd)
    for (uint32_t i = 0; i < m_vecChunk; ++i)
      queueDma(DmaKind::VEC_B, m_desc.srcB + (m_vecOffset + i) * 4, i);

  startDma(/*writing=*/false);
  m_state = State::DMA_IN;
}

void TensorUnit::vectorCompute() {
  const bool isAdd = static_cast<Opcode>(m_desc.opcode) == Opcode::ADD;
  clearPEs();
  for (uint32_t i = 0; i < m_vecChunk; ++i) {
    int32_t v;
    if (isAdd) {
      v = static_cast<int32_t>(static_cast<uint32_t>(m_vecA[i]) +
                               static_cast<uint32_t>(m_vecB[i]));
    } else {
      v = m_vecA[i] < 0 ? 0 : m_vecA[i];
    }
    m_out[i] = v;
    // Visualize the array acting as a SIMD unit: lane i shows its result.
    m_pes[i].acc = v;
    m_pes[i].aValid = true;
  }
}

void TensorUnit::beginVectorWriteback() {
  m_dmaQueue.clear();
  for (uint32_t i = 0; i < m_vecChunk; ++i)
    queueDma(DmaKind::OUT_INT32, m_desc.dst + (m_vecOffset + i) * 4, i);
  startDma(/*writing=*/true);
  m_state = State::DMA_OUT;
}

// ---------------------------------------------------------------------------
// DMA engine
// ---------------------------------------------------------------------------

void TensorUnit::queueDma(DmaKind kind, uint32_t addr, uint32_t index) {
  m_dmaQueue.push_back(DmaElem{addr, index, kind});
}

unsigned TensorUnit::dmaElemBytes(DmaKind kind) {
  // int8 matrix elements are byte transfers; everything else is a word.
  return (kind == DmaKind::A_INT8 || kind == DmaKind::B_INT8) ? 1u : 4u;
}

void TensorUnit::startDma(bool writing) {
  m_counters.dmaTransfers++;
  m_dmaPos = 0;
  m_dmaBytesDone = 0;
  m_dmaBytesTotal = 0;
  m_dmaWriting = writing;
  for (const auto &e : m_dmaQueue)
    m_dmaBytesTotal += dmaElemBytes(e.kind);
}

bool TensorUnit::dmaStep() {
  uint32_t budget = m_dmaWordsPerCycle * 4;
  while (m_dmaPos < m_dmaQueue.size() && budget > 0 && !m_busFault) {
    const DmaElem &e = m_dmaQueue[m_dmaPos];
    const unsigned bytes = dmaElemBytes(e.kind);
    if (bytes > budget)
      break;
    switch (e.kind) {
    case DmaKind::A_INT8:
      m_bufA[e.index] = static_cast<int8_t>(rd8(e.addr));
      break;
    case DmaKind::B_INT8:
      m_bufB[e.index] = static_cast<int8_t>(rd8(e.addr));
      break;
    case DmaKind::VEC_A:
      m_vecA[e.index] = static_cast<int32_t>(rd32(e.addr));
      break;
    case DmaKind::VEC_B:
      m_vecB[e.index] = static_cast<int32_t>(rd32(e.addr));
      break;
    case DmaKind::OUT_INT32:
      wr32(e.addr, static_cast<uint32_t>(m_out[e.index]));
      break;
    }
    if (m_busFault)
      break; // refused: the element was not transferred
    budget -= bytes;
    m_dmaBytesDone += bytes;
    m_counters.dmaBytes += bytes;
    if (e.kind == DmaKind::OUT_INT32)
      m_counters.dmaBytesOut += bytes;
    else
      m_counters.dmaBytesIn += bytes;
    m_dmaPos++;
  }
  return m_dmaPos >= m_dmaQueue.size();
}

void TensorUnit::clearPEs() {
  for (auto &pe : m_pes) {
    pe.a = 0;
    pe.acc = 0;
    pe.aValid = false;
  }
}

} // namespace npu
} // namespace Ripes

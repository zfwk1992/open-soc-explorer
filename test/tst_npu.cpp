#include <QElapsedTimer>
#include <QFile>
#include <QPixmap>
#include <QtTest/QTest>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <future>
#include <vector>

#include "accel/npu/tensorunit.h"
#include "io/iomanager.h"
#include "io/ioregistry.h"
#include "io/iotensorunit.h"
#include "processorhandler.h"
#include "processorregistry.h"
#include "ripessettings.h"
#include "syscall/systemio.h"

using namespace Ripes;
using namespace Ripes::npu;

namespace {

/// A flat little-endian byte memory standing in for the Ripes bus.
struct SimMemory {
  std::vector<uint8_t> bytes;
  explicit SimMemory(size_t size = 1 << 16) : bytes(size, 0) {}

  uint32_t read(uint32_t addr, unsigned n) const {
    uint32_t v = 0;
    for (unsigned i = 0; i < n; ++i)
      v |= static_cast<uint32_t>(bytes.at(addr + i)) << (8 * i);
    return v;
  }
  void write(uint32_t addr, uint32_t value, unsigned n) {
    for (unsigned i = 0; i < n; ++i)
      bytes.at(addr + i) = static_cast<uint8_t>(value >> (8 * i));
  }
  void write32(uint32_t addr, uint32_t value) { write(addr, value, 4); }
  int32_t read32s(uint32_t addr) const {
    return static_cast<int32_t>(read(addr, 4));
  }
  void writeDescriptor(uint32_t addr, const Descriptor &d) {
    for (unsigned i = 0; i < kDescriptorWords; ++i)
      write32(addr + i * 4, d.word(i));
  }

  MemoryCallbacks callbacks() {
    MemoryCallbacks cb;
    cb.read = [this](uint32_t a, unsigned n) { return read(a, n); };
    cb.write = [this](uint32_t a, uint32_t v, unsigned n) { write(a, v, n); };
    return cb;
  }
};

// Memory layout used by the tests.
constexpr uint32_t kQueueBase = 0x1000;
constexpr uint32_t kA = 0x2000;
constexpr uint32_t kB = 0x3000;
constexpr uint32_t kC = 0x4000;
constexpr uint32_t kD = 0x5000;

/// Deterministic int8 pattern in [-127, 127] whose sign flips with the row, so
/// that products (and therefore sums) of both signs occur.
int8_t pattern(unsigned i, unsigned j, int seed) {
  int v = (static_cast<int>(i * 37 + j * 53 + seed * 71) % 255) - 127;
  if (i & 1)
    v = -v;
  return static_cast<int8_t>(v);
}

/// Reference C[M][N] = A[M][K] x B[K][N] with int8 inputs and int32 output.
std::vector<int32_t> refMatmul(const std::vector<int8_t> &A,
                               const std::vector<int8_t> &B, unsigned M,
                               unsigned N, unsigned K) {
  std::vector<int32_t> C(M * N, 0);
  for (unsigned m = 0; m < M; ++m)
    for (unsigned n = 0; n < N; ++n) {
      int32_t acc = 0;
      for (unsigned k = 0; k < K; ++k)
        acc += static_cast<int32_t>(A[m * K + k]) * static_cast<int32_t>(B[k * N + n]);
      C[m * N + n] = acc;
    }
  return C;
}

struct MatmulFixture {
  std::vector<int8_t> A, B;
  std::vector<int32_t> C;
  MatmulFixture(SimMemory &mem, unsigned M, unsigned N, unsigned K, int seed) {
    A.resize(M * K);
    B.resize(K * N);
    for (unsigned m = 0; m < M; ++m)
      for (unsigned k = 0; k < K; ++k) {
        A[m * K + k] = pattern(m, k, seed);
        mem.write(kA + m * K + k, static_cast<uint8_t>(A[m * K + k]), 1);
      }
    for (unsigned k = 0; k < K; ++k)
      for (unsigned n = 0; n < N; ++n) {
        B[k * N + n] = pattern(k, n, seed + 1);
        mem.write(kB + k * N + n, static_cast<uint8_t>(B[k * N + n]), 1);
      }
    C = refMatmul(A, B, M, N, K);
  }
};

Descriptor matmulDesc(uint32_t M, uint32_t N, uint32_t K, uint32_t flags = 0) {
  Descriptor d;
  d.opcode = static_cast<uint32_t>(Opcode::MATMUL);
  d.srcA = kA;
  d.srcB = kB;
  d.dst = kC;
  d.M = M;
  d.N = N;
  d.K = K;
  d.flags = flags;
  return d;
}

/// Sets up a unit with a ring of `slots` descriptors at kQueueBase.
void setupQueue(TensorUnit &unit, SimMemory &mem, uint32_t nslots) {
  unit.setMemory(mem.callbacks());
  unit.setQueueBase(kQueueBase);
  unit.setQueueSize(nslots);
}

} // namespace

class tst_NPU : public QObject {
  Q_OBJECT

private slots:
  void tst_descriptorLayout();
  void tst_matmul4x4();
  void tst_matmulTiled();
  void tst_matmulTiled8x8Array();
  void tst_matmulReluFused();
  void tst_relu();
  void tst_add();
  void tst_ringWraparound();
  void tst_irqW1C();
  void tst_invalidDescriptor();
  void tst_dmaBandwidth();
  void tst_softReset();
  // Bus faults, counters and save/restore (model level).
  void tst_busFaultModel();
  void tst_dmaCounters();
  void tst_saveRestore();
  // End-to-end: the peripheral inside a real Ripes processor.
  void tst_e2eSelfMappedQueue();
  void tst_e2eSelfMappedDst();
  void tst_e2eReverse();
  void tst_e2eReverseSameCycleStore();
  void tst_e2eWidget();
  void tst_e2eBatchExercise();

private:
  void ensureE2E();
  void loadAssemblySource(const QString &src);
  void loadAssemblyFile(const QString &path);
  /// Clocks the processor on a worker thread; aborts the test binary if the
  /// cycles do not complete in time (i.e. the simulation deadlocked).
  void clockGuarded(unsigned cycles);
  uint32_t reg(unsigned r) const;
  void wreg(unsigned r, uint32_t v) const;
  AInt symbol(const QString &name) const;

  IOTensorUnit *m_npu = nullptr;
  AInt m_base = 0;
  QString m_console;
};

void tst_NPU::tst_descriptorLayout() {
  QCOMPARE(kDescriptorBytes, 32u);
  Descriptor d;
  for (unsigned i = 0; i < kDescriptorWords; ++i)
    d.setWord(i, 0x100 + i);
  QCOMPARE(d.opcode, 0x100u);
  QCOMPARE(d.srcA, 0x101u);
  QCOMPARE(d.srcB, 0x102u);
  QCOMPARE(d.dst, 0x103u);
  QCOMPARE(d.M, 0x104u);
  QCOMPARE(d.N, 0x105u);
  QCOMPARE(d.K, 0x106u);
  QCOMPARE(d.flags, 0x107u);
  for (unsigned i = 0; i < kDescriptorWords; ++i)
    QCOMPARE(d.word(i), 0x100u + i);

  // Little-endian in memory, field i at byte offset 4*i.
  SimMemory mem;
  mem.writeDescriptor(kQueueBase, d);
  QCOMPARE(mem.bytes[kQueueBase + 0], uint8_t(0x00));
  QCOMPARE(mem.bytes[kQueueBase + 1], uint8_t(0x01));
  QCOMPARE(mem.bytes[kQueueBase + 4], uint8_t(0x01));
  QCOMPARE(mem.bytes[kQueueBase + 5], uint8_t(0x01));
  QCOMPARE(mem.read(kQueueBase + 28, 4), 0x107u);
}

void tst_NPU::tst_matmul4x4() {
  SimMemory mem;
  TensorUnit unit(4, 1);
  setupQueue(unit, mem, 4);
  MatmulFixture fx(mem, 4, 4, 4, 1);

  mem.writeDescriptor(kQueueBase, matmulDesc(4, 4, 4));
  QVERIFY(unit.idle());
  unit.writeTail(1);
  QVERIFY(!unit.queueEmpty());
  QVERIFY(unit.runUntilIdle());

  for (unsigned i = 0; i < 16; ++i)
    QCOMPARE(mem.read32s(kC + i * 4), fx.C[i]);

  QCOMPARE(unit.head(), 1u);
  QCOMPARE(unit.tail(), 1u);
  QCOMPARE(unit.counters().macs, uint64_t(4 * 4 * 4));
  QCOMPARE(unit.counters().descriptorsDone, uint64_t(1));
  // Descriptor (32 B) + A (16 B) + B (16 B) + C (64 B).
  QCOMPARE(unit.counters().dmaBytes, uint64_t(32 + 16 + 16 + 64));
  QVERIFY(unit.counters().busyCycles > 0);
  QVERIFY(unit.counters().busyCycles <= unit.counters().cycles);
  QVERIFY(unit.status() & Status::QUEUE_EMPTY);
  QVERIFY(!(unit.status() & Status::BUSY));
  QVERIFY(!(unit.status() & Status::ERROR));
}

void tst_NPU::tst_matmulTiled() {
  // M=8, N=6, K=5 on a 4x4 array: partial tiles in every dimension.
  SimMemory mem;
  TensorUnit unit(4, 1);
  setupQueue(unit, mem, 4);
  const unsigned M = 8, N = 6, K = 5;
  MatmulFixture fx(mem, M, N, K, 2);

  mem.writeDescriptor(kQueueBase, matmulDesc(M, N, K));
  unit.writeTail(1);
  QVERIFY(unit.runUntilIdle());

  for (unsigned i = 0; i < M * N; ++i)
    QCOMPARE(mem.read32s(kC + i * 4), fx.C[i]);
  QCOMPARE(unit.counters().macs, uint64_t(M * N * K));
  QCOMPARE(unit.tileInfo().mTiles, 2u);
  QCOMPARE(unit.tileInfo().nTiles, 2u);
  QCOMPARE(unit.tileInfo().kTiles, 2u);
}

void tst_NPU::tst_matmulTiled8x8Array() {
  // Same problem on an 8x8 array with a wider DMA: one tile in M, N and K.
  SimMemory mem;
  TensorUnit unit(8, 4);
  setupQueue(unit, mem, 4);
  const unsigned M = 8, N = 6, K = 5;
  MatmulFixture fx(mem, M, N, K, 3);

  mem.writeDescriptor(kQueueBase, matmulDesc(M, N, K));
  unit.writeTail(1);
  QVERIFY(unit.runUntilIdle());

  for (unsigned i = 0; i < M * N; ++i)
    QCOMPARE(mem.read32s(kC + i * 4), fx.C[i]);
  QCOMPARE(unit.counters().macs, uint64_t(M * N * K));
  QCOMPARE(unit.tileInfo().mTiles, 1u);
  QCOMPARE(unit.tileInfo().nTiles, 1u);
  QCOMPARE(unit.tileInfo().kTiles, 1u);
}

void tst_NPU::tst_matmulReluFused() {
  SimMemory mem;
  TensorUnit unit(4, 1);
  setupQueue(unit, mem, 4);
  const unsigned M = 5, N = 3, K = 7;
  MatmulFixture fx(mem, M, N, K, 4);

  mem.writeDescriptor(kQueueBase, matmulDesc(M, N, K, DescFlags::RELU_FUSED));
  unit.writeTail(1);
  QVERIFY(unit.runUntilIdle());

  bool sawNegative = false;
  for (unsigned i = 0; i < M * N; ++i) {
    const int32_t expected = fx.C[i] < 0 ? 0 : fx.C[i];
    sawNegative |= fx.C[i] < 0;
    QCOMPARE(mem.read32s(kC + i * 4), expected);
  }
  QVERIFY2(sawNegative, "fixture should contain negative products");
}

void tst_NPU::tst_relu() {
  SimMemory mem;
  TensorUnit unit(4, 1);
  setupQueue(unit, mem, 4);
  // 3 x 7 = 21 elements: more than one 16-element chunk. In place (dst ==
  // srcA) must work since every chunk is read before it is written.
  const unsigned M = 3, N = 7;
  std::vector<int32_t> in(M * N);
  for (unsigned i = 0; i < M * N; ++i) {
    in[i] = static_cast<int32_t>(i) * 1000 - 9000; // -9000 .. 11000
    mem.write32(kA + i * 4, static_cast<uint32_t>(in[i]));
  }
  Descriptor d;
  d.opcode = static_cast<uint32_t>(Opcode::RELU);
  d.srcA = kA;
  d.dst = kA;
  d.M = M;
  d.N = N;
  mem.writeDescriptor(kQueueBase, d);
  unit.writeTail(1);
  QVERIFY(unit.runUntilIdle());

  for (unsigned i = 0; i < M * N; ++i)
    QCOMPARE(mem.read32s(kA + i * 4), in[i] < 0 ? 0 : in[i]);
  QCOMPARE(unit.counters().macs, uint64_t(0));
  // 32 B descriptor + 21 words in + 21 words out.
  QCOMPARE(unit.counters().dmaBytes, uint64_t(32 + 21 * 4 + 21 * 4));
}

void tst_NPU::tst_add() {
  SimMemory mem;
  TensorUnit unit(4, 2);
  setupQueue(unit, mem, 4);
  const unsigned M = 4, N = 5;
  for (unsigned i = 0; i < M * N; ++i) {
    mem.write32(kA + i * 4, static_cast<uint32_t>(int32_t(i) * 3 - 20));
    mem.write32(kB + i * 4, static_cast<uint32_t>(-int32_t(i) * 5 + 7));
  }
  Descriptor d;
  d.opcode = static_cast<uint32_t>(Opcode::ADD);
  d.srcA = kA;
  d.srcB = kB;
  d.dst = kD;
  d.M = M;
  d.N = N;
  mem.writeDescriptor(kQueueBase, d);
  unit.writeTail(1);
  QVERIFY(unit.runUntilIdle());

  for (unsigned i = 0; i < M * N; ++i)
    QCOMPARE(mem.read32s(kD + i * 4), (int32_t(i) * 3 - 20) + (-int32_t(i) * 5 + 7));
}

void tst_NPU::tst_ringWraparound() {
  SimMemory mem;
  TensorUnit unit(4, 1);
  setupQueue(unit, mem, 3); // slots 0, 1, 2

  // Each descriptor adds src vectors into a distinct destination so we can
  // tell which ones ran.
  const unsigned count = 4;
  for (unsigned i = 0; i < count; ++i) {
    mem.write32(kA + i * 4, 10 + i);
    mem.write32(kB + i * 4, 100 * (i + 1));
  }
  auto addDesc = [](unsigned i) {
    Descriptor d;
    d.opcode = static_cast<uint32_t>(Opcode::ADD);
    d.srcA = kA + i * 4;
    d.srcB = kB + i * 4;
    d.dst = kD + i * 4;
    d.M = 1;
    d.N = 1;
    return d;
  };

  // Submit two (slots 0 and 1), tail = 2.
  mem.writeDescriptor(kQueueBase + 0 * 32, addDesc(0));
  mem.writeDescriptor(kQueueBase + 1 * 32, addDesc(1));
  unit.writeTail(2);
  QVERIFY(unit.runUntilIdle());
  QCOMPARE(unit.head(), 2u);
  QCOMPARE(unit.counters().descriptorsDone, uint64_t(2));

  // Submit two more: slot 2 and then slot 0 again (wrap), tail wraps to 1.
  mem.writeDescriptor(kQueueBase + 2 * 32, addDesc(2));
  mem.writeDescriptor(kQueueBase + 0 * 32, addDesc(3));
  unit.writeTail(1);
  QVERIFY(!unit.queueEmpty());
  QVERIFY(unit.runUntilIdle());
  QCOMPARE(unit.head(), 1u);
  QCOMPARE(unit.tail(), 1u);
  QCOMPARE(unit.counters().descriptorsDone, uint64_t(4));

  for (unsigned i = 0; i < count; ++i)
    QCOMPARE(mem.read32s(kD + i * 4), int32_t(10 + i + 100 * (i + 1)));

  // TAIL writes are taken modulo QUEUE_SIZE.
  unit.writeTail(4);
  QCOMPARE(unit.tail(), 1u);
  QVERIFY(unit.queueEmpty());
}

void tst_NPU::tst_irqW1C() {
  SimMemory mem;
  TensorUnit unit(4, 1);
  setupQueue(unit, mem, 4);
  int callbacks = 0;
  unit.setInterruptCallback([&callbacks] { callbacks++; });

  Descriptor nop;
  nop.opcode = static_cast<uint32_t>(Opcode::NOP);
  mem.writeDescriptor(kQueueBase, nop);

  // Interrupts disabled: status bits are still latched, no callback.
  unit.setIrqEnable(0);
  unit.writeTail(1);
  QVERIFY(unit.runUntilIdle());
  QCOMPARE(unit.irqStatus(), Irq::DESC_DONE | Irq::QUEUE_EMPTY);
  QVERIFY(!unit.irqPending());
  QCOMPARE(callbacks, 0);

  // Enabling afterwards makes the latched bits pending (level semantics).
  unit.setIrqEnable(Irq::DESC_DONE);
  QVERIFY(unit.irqPending());

  // W1C: clearing an unrelated bit leaves DESC_DONE set; writing zeros does
  // nothing.
  unit.clearIrqStatus(Irq::QUEUE_EMPTY);
  QCOMPARE(unit.irqStatus(), Irq::DESC_DONE);
  unit.clearIrqStatus(0);
  QCOMPARE(unit.irqStatus(), Irq::DESC_DONE);
  unit.clearIrqStatus(Irq::DESC_DONE);
  QCOMPARE(unit.irqStatus(), 0u);
  QVERIFY(!unit.irqPending());

  // With IRQ enabled, completing a descriptor invokes the callback once.
  mem.writeDescriptor(kQueueBase + 32, nop);
  unit.writeTail(2);
  QVERIFY(unit.runUntilIdle());
  QCOMPARE(callbacks, 1);
  QVERIFY(unit.irqPending());
  unit.clearIrqStatus(Irq::ALL);
  QVERIFY(!unit.irqPending());

  // NO_IRQ suppresses DESC_DONE but not QUEUE_EMPTY.
  Descriptor quiet = nop;
  quiet.flags = DescFlags::NO_IRQ;
  mem.writeDescriptor(kQueueBase + 2 * 32, quiet);
  unit.writeTail(3);
  QVERIFY(unit.runUntilIdle());
  QCOMPARE(unit.irqStatus(), Irq::QUEUE_EMPTY);
  QCOMPARE(callbacks, 1);
}

void tst_NPU::tst_invalidDescriptor() {
  SimMemory mem;
  TensorUnit unit(4, 1);
  setupQueue(unit, mem, 4);

  Descriptor bad;
  bad.opcode = 0x55;
  mem.writeDescriptor(kQueueBase, bad);
  Descriptor zeroDim = matmulDesc(0, 4, 4);
  mem.writeDescriptor(kQueueBase + 32, zeroDim);
  unit.writeTail(2);
  QVERIFY(unit.runUntilIdle());

  // Both were rejected, but the ring still advanced past them.
  QCOMPARE(unit.head(), 2u);
  QCOMPARE(unit.counters().descriptorsDone, uint64_t(2));
  QVERIFY(unit.status() & Status::ERROR);
  QVERIFY(unit.irqStatus() & Irq::ERROR);
  QVERIFY(!(unit.irqStatus() & Irq::DESC_DONE));
  QCOMPARE(unit.counters().macs, uint64_t(0));
}

void tst_NPU::tst_dmaBandwidth() {
  // The same job must take fewer cycles with a wider DMA, and the result must
  // not depend on the bandwidth.
  const unsigned M = 6, N = 6, K = 6;
  uint64_t narrowCycles = 0, wideCycles = 0;
  std::vector<int32_t> narrowC, wideC;

  for (int pass = 0; pass < 2; ++pass) {
    SimMemory mem;
    TensorUnit unit(4, pass == 0 ? 1 : 8);
    setupQueue(unit, mem, 2);
    MatmulFixture fx(mem, M, N, K, 5);
    mem.writeDescriptor(kQueueBase, matmulDesc(M, N, K));
    unit.writeTail(1);
    QVERIFY(unit.runUntilIdle());
    std::vector<int32_t> C(M * N);
    for (unsigned i = 0; i < M * N; ++i)
      C[i] = mem.read32s(kC + i * 4);
    QVERIFY(C == fx.C);
    if (pass == 0) {
      narrowCycles = unit.counters().busyCycles;
      narrowC = C;
    } else {
      wideCycles = unit.counters().busyCycles;
      wideC = C;
    }
  }
  QVERIFY(narrowC == wideC);
  QVERIFY2(wideCycles < narrowCycles, "wider DMA should finish sooner");
}

void tst_NPU::tst_softReset() {
  SimMemory mem;
  TensorUnit unit(4, 1);
  setupQueue(unit, mem, 4);
  MatmulFixture fx(mem, 8, 8, 8, 6);
  mem.writeDescriptor(kQueueBase, matmulDesc(8, 8, 8));
  unit.writeTail(1);
  unit.step(20);
  QVERIFY(!unit.idle());
  const uint64_t cyclesBefore = unit.counters().cycles;

  unit.writeCtrl(Ctrl::SOFT_RESET);
  QVERIFY(unit.idle());
  QCOMPARE(unit.head(), 0u);
  QCOMPARE(unit.tail(), 0u);
  QCOMPARE(unit.irqStatus(), 0u);
  QCOMPARE(unit.queueBase(), kQueueBase); // configuration survives
  QCOMPARE(unit.queueSize(), 4u);
  QCOMPARE(unit.counters().cycles, cyclesBefore); // counters survive

  unit.writeCtrl(Ctrl::CLEAR_COUNTERS);
  QCOMPARE(unit.counters().cycles, uint64_t(0));

  // The unit is usable again afterwards.
  unit.writeTail(1);
  QVERIFY(unit.runUntilIdle());
  for (unsigned i = 0; i < 64; ++i)
    QCOMPARE(mem.read32s(kC + i * 4), fx.C[i]);
}

// ---------------------------------------------------------------------------
// Model-level tests for the review fixes
// ---------------------------------------------------------------------------

void tst_NPU::tst_busFaultModel() {
  // Accesses to [kFault, kFault+0x100) are refused by the "bus", as the Ripes
  // peripheral does for its own register block.
  constexpr uint32_t kFault = 0xF000;
  SimMemory mem;
  TensorUnit unit(4, 1);
  MemoryCallbacks cb;
  int faultingAccesses = 0;
  cb.read = [&](uint32_t a, unsigned n) -> uint32_t {
    if (a >= kFault && a < kFault + 0x100) {
      faultingAccesses++;
      unit.reportBusError(a);
      return 0;
    }
    return mem.read(a, n);
  };
  cb.write = [&](uint32_t a, uint32_t v, unsigned n) {
    if (a >= kFault && a < kFault + 0x100) {
      faultingAccesses++;
      unit.reportBusError(a);
      return;
    }
    mem.write(a, v, n);
  };
  unit.setMemory(cb);

  // 1) The ring itself lies in the faulting region.
  unit.setQueueBase(kFault);
  unit.setQueueSize(4);
  unit.setIrqEnable(Irq::ALL);
  unit.writeTail(1);
  QVERIFY(unit.runUntilIdle(1000));
  QCOMPARE(unit.head(), 1u);
  QVERIFY(unit.status() & Status::ERROR);
  QVERIFY(unit.status() & Status::DMA_FAULT);
  QVERIFY(unit.irqStatus() & Irq::ERROR);
  QCOMPARE(unit.errorCause(), ErrorCause::BUS_FAULT);
  QCOMPARE(unit.faultAddr(), kFault);
  QCOMPARE(faultingAccesses, 1); // the rest of the fetch is not attempted
  QCOMPARE(unit.counters().dmaBytes, uint64_t(0));

  // 2) A valid ring, but the destination is in the faulting region; the
  // following descriptor must still run correctly.
  unit.writeCtrl(Ctrl::SOFT_RESET);
  unit.setQueueBase(kQueueBase);
  MatmulFixture fx(mem, 4, 4, 4, 9);
  Descriptor bad = matmulDesc(4, 4, 4);
  bad.dst = kFault + 0x10;
  mem.writeDescriptor(kQueueBase, bad);
  mem.writeDescriptor(kQueueBase + 32, matmulDesc(4, 4, 4));
  unit.writeTail(2);
  unsigned guard = 0;
  while (unit.head() == 0 && guard++ < 10000) // first descriptor only
    unit.step();
  QVERIFY(unit.status() & Status::DMA_FAULT);
  QCOMPARE(unit.faultAddr(), kFault + 0x10);
  QVERIFY(unit.runUntilIdle());
  QCOMPARE(unit.head(), 2u);
  QVERIFY(!(unit.status() & Status::ERROR)); // cleared by the next fetch
  QCOMPARE(unit.errorCause(), ErrorCause::NONE);
  for (unsigned i = 0; i < 16; ++i)
    QCOMPARE(mem.read32s(kC + i * 4), fx.C[i]);
  QCOMPARE(unit.counters().errors, uint64_t(2));
}

void tst_NPU::tst_dmaCounters() {
  SimMemory mem;
  TensorUnit unit(4, 1);
  setupQueue(unit, mem, 4);
  MatmulFixture fx(mem, 4, 4, 4, 1);
  mem.writeDescriptor(kQueueBase, matmulDesc(4, 4, 4));
  unit.writeTail(1);
  QVERIFY(unit.runUntilIdle());
  const auto &c = unit.counters();
  QCOMPARE(c.dmaBytesIn, uint64_t(32 + 16 + 16));
  QCOMPARE(c.dmaBytesOut, uint64_t(64));
  QCOMPARE(c.dmaBytesIn + c.dmaBytesOut, c.dmaBytes);
  QCOMPARE(c.dmaTransfers, uint64_t(3)); // fetch, tile load, writeback
  uint64_t sum = 0;
  for (auto v : c.stateCycles)
    sum += v;
  QCOMPARE(sum, c.cycles);
  QCOMPARE(c.cycles - c.stateCycles[0], c.busyCycles);
  // Table in docs/arch/tensor_cpu.md (N=4, W=1, 4x4x4).
  QCOMPARE(c.stateCycles[static_cast<unsigned>(State::FETCH_DESC)],
           uint64_t(8));
  QCOMPARE(c.stateCycles[static_cast<unsigned>(State::DMA_IN)], uint64_t(8));
  QCOMPARE(c.stateCycles[static_cast<unsigned>(State::COMPUTE)],
           uint64_t(14));
  QCOMPARE(c.stateCycles[static_cast<unsigned>(State::DMA_OUT)],
           uint64_t(16));
  QCOMPARE(c.busyCycles, uint64_t(47));
}

void tst_NPU::tst_saveRestore() {
  // Save after every cycle, then restore a mid point (undoing the DMA writes
  // made since) and check that re-running reproduces the original result.
  for (unsigned n : {4u, 32u}) {
    SimMemory mem;
    TensorUnit unit(n, 1);
    setupQueue(unit, mem, 4);
    MatmulFixture fx(mem, 6, 5, 7, 11);
    mem.writeDescriptor(kQueueBase, matmulDesc(6, 5, 7));
    unit.setWriteUndoLogging(true);
    unit.writeTail(1);

    std::vector<SavedState> saves;
    std::vector<Snapshot> snaps;
    std::vector<std::vector<DmaWriteUndo>> writes;
    saves.push_back(unit.saveState());
    snaps.push_back(unit.snapshot());
    writes.emplace_back();
    size_t maxBytes = 0;
    while (!(unit.idle() && unit.queueEmpty())) {
      unit.step();
      saves.push_back(unit.saveState());
      snaps.push_back(unit.snapshot());
      writes.push_back(unit.takeWriteLog());
      maxBytes = std::max(maxBytes, saves.back().bytes());
    }
    // Idle cycles share the buffer block with the previous save.
    unit.step();
    const SavedState idle1 = unit.saveState();
    unit.step();
    const SavedState idle2 = unit.saveState();
    QVERIFY(idle1.buffers == idle2.buffers);
    qInfo().nospace() << "N=" << n << ": snapshot <= " << maxBytes
                      << " bytes per busy cycle, " << idle2.bytes(false)
                      << " bytes per idle cycle";

    const size_t back = saves.size() / 2;
    for (size_t i = writes.size(); i-- > back + 1;)
      for (auto it = writes[i].rbegin(); it != writes[i].rend(); ++it)
        mem.write(it->addr, it->oldValue, it->bytes);
    unit.restoreState(saves[back]);
    const Snapshot s = unit.snapshot();
    QCOMPARE(s.state, snaps[back].state);
    QCOMPARE(s.counters.cycles, snaps[back].counters.cycles);
    QCOMPARE(s.counters.macs, snaps[back].counters.macs);
    QCOMPARE(s.dma.bytesDone, snaps[back].dma.bytesDone);
    QCOMPARE(s.head, snaps[back].head);
    for (size_t i = 0; i < s.pes.size(); ++i)
      QCOMPARE(s.pes[i].acc, snaps[back].pes[i].acc);
    QVERIFY(unit.runUntilIdle());
    for (unsigned i = 0; i < 6 * 5; ++i)
      QCOMPARE(mem.read32s(kC + i * 4), fx.C[i]);
    QCOMPARE(unit.counters().cycles, snaps.back().counters.cycles);
    unit.takeWriteLog();

    // Back to the very first save: the output buffer is untouched again.
    for (size_t i = writes.size(); i-- > 1;)
      for (auto it = writes[i].rbegin(); it != writes[i].rend(); ++it)
        mem.write(it->addr, it->oldValue, it->bytes);
    unit.restoreState(saves.front());
    QCOMPARE(unit.state(), State::IDLE);
    QCOMPARE(unit.head(), 0u);
    QCOMPARE(unit.tail(), 1u);
    for (unsigned i = 0; i < 6 * 5; ++i)
      QCOMPARE(mem.read32s(kC + i * 4), 0);
  }
}

// ---------------------------------------------------------------------------
// End-to-end tests (real processor + IOManager)
// ---------------------------------------------------------------------------

void tst_NPU::ensureE2E() {
  if (m_npu)
    return;
  ProcessorHandler::selectProcessor(ProcessorID::RV32_5S, {"M"});
  m_npu = dynamic_cast<IOTensorUnit *>(
      IOManager::get().createPeripheral(IOType::TENSOR_UNIT));
  QVERIFY(m_npu != nullptr);
  for (const auto &kv : IOManager::get().memoryMap())
    if (kv.second.name == m_npu->name())
      m_base = kv.second.startAddr;
  QVERIFY(m_base != 0);
  QCOMPARE(m_npu->baseAddress(), m_base);
  connect(
      &SystemIO::get(), &SystemIO::doPrint, this,
      [this](const QString &text) {
        QString t = text;
        t.remove(QChar('\0'));
        m_console += t;
      },
      Qt::DirectConnection);
}

void tst_NPU::loadAssemblySource(const QString &src) {
  auto res = ProcessorHandler::getAssembler()->assembleRaw(
      src, &IOManager::get().assemblerSymbols());
  if (res.errors.size() != 0)
    QFAIL(("Could not assemble program:\n" + res.errors.toString())
              .toStdString()
              .c_str());
  ProcessorHandler::loadProgram(std::make_shared<Program>(res.program));
  RipesSettings::getObserver(RIPES_GLOBALSIGNAL_REQRESET)->trigger();
  SystemIO::reset();
  m_console.clear();
}

void tst_NPU::loadAssemblyFile(const QString &path) {
  QFile f(path);
  QVERIFY2(f.open(QIODevice::ReadOnly), path.toStdString().c_str());
  loadAssemblySource(QString::fromUtf8(f.readAll()));
}

void tst_NPU::clockGuarded(unsigned cycles) {
  auto *proc = ProcessorHandler::getProcessorNonConst();
  auto fut = std::async(std::launch::async, [proc, cycles] {
    for (unsigned i = 0; i < cycles; ++i)
      proc->clock();
  });
  if (fut.wait_for(std::chrono::seconds(20)) != std::future_status::ready) {
    // The worker is stuck (e.g. a non-recursive mutex re-entered from a DMA
    // access). Abort instead of hanging the test run forever.
    qFatal("tst_NPU: processor clocking did not finish within 20 s - "
           "deadlock in the Tensor Unit peripheral?");
  }
  fut.get();
  QCoreApplication::processEvents();
}

uint32_t tst_NPU::reg(unsigned r) const {
  return static_cast<uint32_t>(
      ProcessorHandler::getMemory().readMem(m_base + r * 4, 4));
}

void tst_NPU::wreg(unsigned r, uint32_t v) const {
  ProcessorHandler::getMemory().writeMem(m_base + r * 4, v, 4);
}

AInt tst_NPU::symbol(const QString &name) const {
  const auto &program = ProcessorHandler::getProgram();
  if (!program)
    return 0;
  for (const auto &kv : program->symbols)
    if (kv.second.v == name)
      return kv.first;
  return 0;
}

namespace {
// Never-ending program with a descriptor and two 4x4 int8 matrices in .data
// (B is the identity, so C == A).
const char *kSpinProgram = R"(
.data
desc:   .word 1, 0, 0, 0, 4, 4, 4, 0
        .word 0, 0, 0, 0, 0, 0, 0, 0
A:      .byte 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
B:      .byte 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1
C:      .zero 64
.text
main:
    nop
spin:
    j spin
)";
} // namespace

void tst_NPU::tst_e2eSelfMappedQueue() {
  // Review defect: QUEUE_BASE pointing into the NPU's own registers used to
  // deadlock (DMA read -> ioRead -> non-recursive mutex already held).
  ensureE2E();
  loadAssemblySource(kSpinProgram);
  wreg(IOTensorUnit::QUEUE_BASE, static_cast<uint32_t>(m_base));
  wreg(IOTensorUnit::QUEUE_SIZE, 4);
  wreg(IOTensorUnit::IRQ_EN, Irq::ALL);
  wreg(IOTensorUnit::TAIL, 1); // doorbell
  clockGuarded(50);
  QCOMPARE(reg(IOTensorUnit::HEAD), 1u);
  const uint32_t st = reg(IOTensorUnit::STATUS);
  QVERIFY(st & Status::ERROR);
  QVERIFY(st & Status::DMA_FAULT);
  QVERIFY(!(st & Status::BUSY));
  QVERIFY(reg(IOTensorUnit::IRQ_STATUS) & Irq::ERROR);
  QCOMPARE(reg(IOTensorUnit::FAULT_ADDR), static_cast<uint32_t>(m_base));
  // The refused accesses did not modify any register.
  QCOMPARE(reg(IOTensorUnit::QUEUE_BASE), static_cast<uint32_t>(m_base));
  QCOMPARE(reg(IOTensorUnit::QUEUE_SIZE), 4u);
  QCOMPARE(reg(IOTensorUnit::TAIL), 1u);
}

void tst_NPU::tst_e2eSelfMappedDst() {
  // A valid descriptor whose destination is the NPU's own register block:
  // the DMA_OUT writes must be refused (no deadlock, registers unchanged).
  ensureE2E();
  loadAssemblySource(kSpinProgram);
  const AInt desc = symbol("desc");
  const AInt a = symbol("A");
  const AInt b = symbol("B");
  const AInt c = symbol("C");
  QVERIFY(desc != 0 && a != 0 && b != 0 && c != 0);
  auto &mem = ProcessorHandler::getMemory();
  mem.writeMem(desc + 4, a, 4);
  mem.writeMem(desc + 8, b, 4);
  const uint32_t dst = static_cast<uint32_t>(m_base) + 0x0C; // TAIL
  mem.writeMem(desc + 12, dst, 4);

  wreg(IOTensorUnit::QUEUE_BASE, static_cast<uint32_t>(desc));
  wreg(IOTensorUnit::QUEUE_SIZE, 2);
  wreg(IOTensorUnit::TAIL, 1);
  clockGuarded(200);
  QCOMPARE(reg(IOTensorUnit::HEAD), 1u);
  QCOMPARE(reg(IOTensorUnit::TAIL), 1u); // not overwritten by the DMA
  QCOMPARE(reg(IOTensorUnit::QUEUE_SIZE), 2u);
  const uint32_t st = reg(IOTensorUnit::STATUS);
  QVERIFY(st & Status::ERROR);
  QVERIFY(st & Status::DMA_FAULT);
  QCOMPARE(reg(IOTensorUnit::FAULT_ADDR), dst);
  QCOMPARE(reg(IOTensorUnit::MACS), 64u); // the compute itself ran

  // The unit keeps working: slot 1 gets the same job with dst = C.
  for (unsigned i = 0; i < 8; ++i)
    mem.writeMem(desc + 32 + i * 4, mem.readMem(desc + i * 4, 4), 4);
  mem.writeMem(desc + 32 + 12, c, 4);
  wreg(IOTensorUnit::TAIL, 0); // slot 1 submitted, tail wraps to 0
  clockGuarded(200);
  QCOMPARE(reg(IOTensorUnit::HEAD), 0u);
  QVERIFY(!(reg(IOTensorUnit::STATUS) & Status::ERROR));
  for (unsigned i = 0; i < 16; ++i)
    QCOMPARE(mem.readMem(c + i * 4, 4), VInt(i + 1));
}

void tst_NPU::tst_e2eReverse() {
  ensureE2E();
  const int limit =
      RipesSettings::value(RIPES_SETTING_REWINDSTACKSIZE).toInt();
  if (limit < 70)
    QSKIP("rewind stack size setting < 70 cycles");
  loadAssemblyFile(QStringLiteral(RIPES_EXAMPLES_DIR) +
                   "/assembly/npu_matmul.s");
  const AInt cNpu = symbol("C_npu");
  QVERIFY(cNpu != 0);
  auto *proc = ProcessorHandler::getProcessorNonConst();
  auto &mem = ProcessorHandler::getMemory();

  struct Obs {
    uint32_t status, head, cycles, dma, macs, busy, irq;
    std::vector<uint32_t> c;
    bool operator==(const Obs &o) const {
      return status == o.status && head == o.head && cycles == o.cycles &&
             dma == o.dma && macs == o.macs && busy == o.busy &&
             irq == o.irq && c == o.c;
    }
  };
  auto observe = [&] {
    Obs o{reg(IOTensorUnit::STATUS),     reg(IOTensorUnit::HEAD),
          reg(IOTensorUnit::CYCLES),     reg(IOTensorUnit::DMA_BYTES),
          reg(IOTensorUnit::MACS),       reg(IOTensorUnit::BUSY_CYCLES),
          reg(IOTensorUnit::IRQ_STATUS), {}};
    for (unsigned i = 0; i < 16; ++i)
      o.c.push_back(static_cast<uint32_t>(mem.readMem(cNpu + i * 4, 4)));
    return o;
  };

  // Run until the NPU has finished the matmul (HEAD == 1).
  unsigned guard = 0;
  while (reg(IOTensorUnit::HEAD) != 1 && guard++ < 20000)
    proc->clock();
  QCOMPARE(reg(IOTensorUnit::HEAD), 1u);
  // Record a few more cycles: rec[0] = now, rec[i] = after i more cycles.
  std::vector<Obs> rec;
  rec.push_back(observe());
  for (unsigned i = 0; i < 5; ++i) {
    proc->clock();
    rec.push_back(observe());
  }
  QCOMPARE(rec.back().macs, 64u);

  // Reverse cycle by cycle: every step must match the recorded state.
  for (int j = static_cast<int>(rec.size()) - 2; j >= 0; --j) {
    proc->reverseProcessor();
    QCoreApplication::processEvents();
    QVERIFY2(observe() == rec[j],
             qPrintable(QString("mismatch after reversing to cycle %1").arg(j)));
  }
  // Keep reversing into the NPU's busy period: HEAD drops back to 0 and the
  // C_npu words written by DMA_OUT are restored to zero.
  bool sawHead0 = false, sawCZero = false, sawBusy = false;
  Obs prev = observe();
  for (unsigned i = 0; i < 60; ++i) {
    proc->reverseProcessor();
    QCoreApplication::processEvents();
    const Obs now = observe();
    QCOMPARE(now.cycles, prev.cycles - 1); // CYCLES rewinds one per step
    sawHead0 |= now.head == 0;
    sawBusy |= (now.status & Status::BUSY) != 0;
    bool allZero = true;
    for (auto v : now.c)
      allZero &= v == 0;
    sawCZero |= allZero;
    prev = now;
  }
  QVERIFY(sawHead0);
  QVERIFY(sawBusy);
  QVERIFY2(sawCZero, "DMA writes to C_npu were not undone by reverse");

  // Clocking forward again reproduces the same result.
  guard = 0;
  while (!proc->finished() && guard++ < 50000)
    proc->clock();
  QVERIFY(proc->finished());
  QCoreApplication::processEvents();
  QVERIFY2(m_console.contains("Result: OK"), qPrintable(m_console));
  QCOMPARE(reg(IOTensorUnit::MACS), 64u);
}

void tst_NPU::tst_e2eReverseSameCycleStore() {
  // Review defect: the processor rewinds its own stores before the NPU undoes
  // its DMA writes. A word the CPU stored (v0 -> v1) and the DMA then
  // overwrote (v1 -> v2) in the same cycle therefore came back as v1 after a
  // reverse instead of the pre-cycle value v0.
  ensureE2E();
  if (RipesSettings::value(RIPES_SETTING_REWINDSTACKSIZE).toInt() < 10)
    QSKIP("rewind stack size setting < 10 cycles");

  // The host stores a different value to C[0] every cycle (a straight run of
  // stores without hazards issues one per cycle in the 5-stage pipeline)
  // while the NPU computes C = A x I and DMAs the result into C.
  QString src = R"(
.data
desc:   .word 1, 0, 0, 0, 4, 4, 4, 0
        .word 0, 0, 0, 0, 0, 0, 0, 0
A:      .byte 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
B:      .byte 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1
C:      .zero 64
.text
main:
    la a0, C
    li t0, 0x101
    li t1, 0x102
    li t2, 0x103
    li t3, 0x104
    li t4, 0x105
    li t5, 0x106
    li t6, 0x107
)";
  for (int i = 0; i < 400; ++i)
    src += QStringLiteral("    sw t%1, 0(a0)\n").arg(i % 7);
  src += "spin:\n    j spin\n";
  loadAssemblySource(src);

  const AInt desc = symbol("desc");
  const AInt a = symbol("A");
  const AInt b = symbol("B");
  const AInt c = symbol("C");
  QVERIFY(desc != 0 && a != 0 && b != 0 && c != 0);
  auto &mem = ProcessorHandler::getMemory();
  mem.writeMem(desc + 4, a, 4);
  mem.writeMem(desc + 8, b, 4);
  mem.writeMem(desc + 12, c, 4);
  wreg(IOTensorUnit::QUEUE_BASE, static_cast<uint32_t>(desc));
  wreg(IOTensorUnit::QUEUE_SIZE, 2);
  wreg(IOTensorUnit::TAIL, 1); // doorbell

  auto *proc = ProcessorHandler::getProcessorNonConst();
  auto c0 = [&] { return static_cast<uint32_t>(mem.readMem(c, 4)); };
  auto isCpuValue = [](uint32_t v) { return v >= 0x101u && v <= 0x107u; };

  // Clock until the DMA_OUT write of C[0] (= A[0][0] * 1 = 1) lands; hist[i]
  // is C[0] after i cycles.
  std::vector<uint32_t> hist{c0()};
  size_t k = 0;
  for (unsigned i = 0; i < 400 && k == 0; ++i) {
    proc->clock();
    hist.push_back(c0());
    if (hist.back() == 1u)
      k = hist.size() - 1;
  }
  QVERIFY2(k != 0, "the DMA never wrote C[0]");
  // The CPU stored a new value to C[0] in each of the cycles before k, and
  // the store run (400 instructions) continues well past k: cycle k holds a
  // CPU store (v0 -> v1) followed by the DMA write (v1 -> 1).
  QVERIFY(k >= 4 && k < 300);
  for (size_t i = k - 3; i < k; ++i) {
    QVERIFY2(isCpuValue(hist[i]), qPrintable(QString("C[0] = 0x%1 after cycle "
                                                     "%2 is not a CPU store")
                                                 .arg(hist[i], 0, 16)
                                                 .arg(i)));
    QVERIFY(hist[i] != hist[i - 1]);
  }
  const uint32_t preCycle = hist[k - 1];

  // Reversing cycle k must leave the pre-cycle value, not the value the CPU
  // stored during cycle k (which is what the DMA's undo log recorded as the
  // "old" contents).
  proc->reverseProcessor();
  QCoreApplication::processEvents();
  QCOMPARE(c0(), preCycle);

  // Going forward again reproduces the cycle, and the following cycle's CPU
  // store lands normally on top of the DMA result.
  proc->clock();
  QCOMPARE(c0(), 1u);
  proc->clock();
  QVERIFY(isCpuValue(c0()));
}

void tst_NPU::tst_e2eWidget() {
  // Drives the widget with real data and renders it offscreen. If
  // NPU_WIDGET_GRAB_DIR is set, PNGs are written there for inspection.
  ensureE2E();
  loadAssemblyFile(QStringLiteral(RIPES_EXAMPLES_DIR) +
                   "/assembly/npu_matmul.s");
  const QString dir = qEnvironmentVariable("NPU_WIDGET_GRAB_DIR");
  m_npu->resize(1100, 760);
  m_npu->refreshView();
  QPixmap empty = m_npu->grab();
  QVERIFY(!empty.isNull());
  if (!dir.isEmpty())
    empty.save(dir + "/npu_widget_empty.png");

  auto *proc = ProcessorHandler::getProcessorNonConst();
  // Stop in the middle of COMPUTE to capture the systolic array in action.
  const auto stateOf = [this] {
    return (reg(IOTensorUnit::STATUS) & Status::STATE_MASK) >>
           Status::STATE_SHIFT;
  };
  unsigned guard = 0;
  while (stateOf() != static_cast<uint32_t>(State::COMPUTE) &&
         guard++ < 20000)
    proc->clock();
  QCOMPARE(stateOf(), static_cast<uint32_t>(State::COMPUTE));
  for (unsigned i = 0; i < 7; ++i)
    proc->clock();
  m_npu->refreshView();
  QPixmap busy = m_npu->grab();
  QVERIFY(!busy.isNull());
  if (!dir.isEmpty())
    busy.save(dir + "/npu_widget_compute.png");

  guard = 0;
  while (!proc->finished() && guard++ < 50000)
    proc->clock();
  QCoreApplication::processEvents();
  m_npu->refreshView();
  QPixmap done = m_npu->grab();
  QVERIFY(!done.isNull());
  if (!dir.isEmpty())
    done.save(dir + "/npu_widget_done.png");
  QVERIFY2(m_console.contains("Result: OK"), qPrintable(m_console));
  qInfo().noquote() << "npu_matmul.s console:\n" << m_console;
  // The data shown by the widget agrees with the registers.
  const auto vd = m_npu->viewData();
  QCOMPARE(vd.snap.counters.macs, uint64_t(64));
  QCOMPARE(vd.snap.head, 1u);
  QVERIFY(vd.historyFrames > 0);
}

void tst_NPU::tst_e2eBatchExercise() {
  // examples/assembly/exercise_npu_batch.s (user guide chapter 10): two
  // descriptors behind one doorbell, MATMUL with NO_IRQ then in-place RELU.
  ensureE2E();
  loadAssemblyFile(QStringLiteral(RIPES_EXAMPLES_DIR) +
                   "/assembly/exercise_npu_batch.s");
  auto *proc = ProcessorHandler::getProcessorNonConst();
  unsigned guard = 0;
  while (!proc->finished() && guard++ < 50000)
    proc->clock();
  QVERIFY(proc->finished());
  QCoreApplication::processEvents();
  qInfo().noquote() << "exercise_npu_batch.s console:\n" << m_console;
  QVERIFY2(m_console.contains("Result: OK"), qPrintable(m_console));
  const auto field = [this](const QString &label) {
    const int i = m_console.indexOf(label);
    if (i < 0)
      return -1;
    return m_console.mid(i + label.size()).section('\n', 0, 0).trimmed()
        .toInt();
  };
  const int busy = field("NPU busy cycles:");
  const int irq = field("IRQ_STATUS:");
  QCOMPARE(field("NPU MACs:"), 64);
  QCOMPARE(irq, 3); // DESC_DONE | QUEUE_EMPTY, only from slot 1
  QVERIFY(busy > 0);
  // The program acknowledged the bits (W1C) before exiting.
  QCOMPARE(reg(IOTensorUnit::IRQ_STATUS), 0u);
  QCOMPARE(reg(IOTensorUnit::HEAD), 2u);
}

QTEST_MAIN(tst_NPU)
#include "tst_npu.moc"

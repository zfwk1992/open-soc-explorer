#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QPixmap>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QtTest/QTest>

#include <atomic>
#include <cstdlib>
#include <thread>

#include "accel/gpu/simtgpu.h"
#include "cachesim/cacheplotwidget.h"
#include "cachesim/cachesim.h"
#include "io/iogpu.h"
#include "io/iogpuwidget.h"
#include "io/iomanager.h"
#include "io/ioregistry.h"
#include "io/memorymapmodel.h"
#include "processorhandler.h"
#include "processorregistry.h"
#include "ripessettings.h"
#include "syscall/systemio.h"

/**
 * End-to-end test of the two MMIO accelerator peripherals (SIMT GPU and
 * Tensor Unit). The example programs examples/assembly/gpu_vecadd.s and
 * examples/assembly/npu_matmul.s are assembled with the peripheral symbols
 * exported by the IOManager and executed on the real RV32_5S (+M) processor
 * model, with the peripherals stepped in lockstep by
 * ProcessorHandler::processorClocked exactly as in the GUI/CLI.
 *
 * A third test drives the GPU registers directly through the processor memory
 * interface (i.e. through the IO region installed by the IOManager) and checks
 * that STATUS.DONE goes high after clocking the processor.
 */

using namespace Ripes;

namespace {
const QString s_examplesDir = RIPES_EXAMPLES_DIR;
// Upper bound on simulated cycles per program; the examples take a few
// thousand cycles.
constexpr unsigned s_maxCycles = 200000;
// Upper bound on wall-clock time per program.
constexpr qint64 s_maxWallMs = 20000;

// The vecadd kernel, in GPU assembly. It is assembled with the C++ kernel
// assembler (gpu::assembleKernel) instead of hand-copied machine code.
const char *const kVecAddSource = R"(
    S_TID   R1
    S_BID   R2
    S_NTID  R3
    MUL     R4, R2, R3
    ADD     R4, R4, R1
    S_ARG   R5, 3
    SETP.GE P0, R4, R5
    @P0 EXIT
    SHL     R4, R4, 2
    S_ARG   R6, 0
    S_ARG   R7, 1
    S_ARG   R8, 2
    ADD     R6, R6, R4
    ADD     R7, R7, R4
    ADD     R8, R8, R4
    LDG     R9, [R6]
    LDG     R10, [R7]
    ADD     R11, R9, R10
    STG     [R8], R11
    EXIT
)";

/// Assembles GPU kernel source into RISC-V ".word" directives.
QString kernelWords(const char *source) {
  std::string err;
  const auto words = Ripes::gpu::assembleKernel(source, &err);
  if (!err.empty())
    qFatal("kernel assembly failed: %s", err.c_str());
  QString out;
  for (size_t i = 0; i < words.size(); ++i)
    out += QStringLiteral("    .word 0x%1    # %2: %3\n")
               .arg(words[i], 8, 16, QChar('0'))
               .arg(i)
               .arg(QString::fromStdString(Ripes::gpu::disassemble(words[i])));
  return out;
}

/// A host program that spins forever; the kernel and data live in .data.
QString spinProgram(const QString &kernelText, const QString &data) {
  return QString(".data\n.align 4\nkernel:\n") + kernelText + data +
         "\n.text\nmain:\n    nop\nspin:\n    j spin\n";
}
} // namespace

class tst_AccelE2E : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void cleanup();
  void testGpuVecAdd();
  void testNpuMatmul();
  void testGpuRegisterLevel();
  void testGpuExampleMatchesSource();
  void testGpuSelfAccessNoDeadlock();
  void testGpuLaunchWhileBusy();
  void testGpuReverse();
  void testGpuWidgetRender();
  void testGpuExercises();
  // GUI regressions (not accelerator specific, but this test already runs
  // the processor with a QApplication).
  void testMemoryMapRangeText();
  void testCachePlotWindow();
  void testCachePlotTraceTruncated();

private:
  /// Clocks the processor @p cycles times, reading one word per cycle
  /// through @p cache, then reports the end of a run as the GUI does.
  void runCacheAccesses(const std::shared_ptr<CacheSim> &cache,
                        unsigned cycles);
  QTemporaryDir m_settingsDir;
  /// Fills the launch registers for kernel/A/B/C of a spin program.
  void setupVecAdd(uint32_t n, uint32_t block);
  /// Clocks until STATUS.DONE or @p maxCycles; returns the cycles clocked.
  unsigned clockUntilDone(unsigned maxCycles);
  /// Address of a symbol of the loaded program (0 if absent).
  AInt symbol(const QString &name) const;
  /// Assembles @p path with the peripheral symbols, loads it and resets.
  void loadAssembly(const QString &path);
  void loadAssemblySource(const QString &src);
  /// Clocks the processor until it reports finished(), a cycle limit or a
  /// wall-clock limit is hit. Returns the number of cycles executed.
  unsigned runToCompletion(bool &timedOut);
  /// Base address of peripheral @p p in the memory map.
  AInt baseOf(IOBase *p) const;

  IOBase *m_gpu = nullptr;
  IOBase *m_npu = nullptr;
  QString m_console;
};

void tst_AccelE2E::initTestCase() {
  // Keep the test away from the user's real Ripes settings (some tests below
  // change settings): store QSettings as INI files in a temp dir.
  QVERIFY(m_settingsDir.isValid());
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     m_settingsDir.path());

  ProcessorHandler::selectProcessor(ProcessorID::RV32_5S, {"M"});

  // Instantiate the peripherals exactly as the I/O tab does. The IOManager
  // places them in the memory map, hooks them into the processor memory and
  // exports the *_BASE / register / bit symbols to the assembler.
  m_gpu = IOManager::get().createPeripheral(IOType::GPU);
  m_npu = IOManager::get().createPeripheral(IOType::TENSOR_UNIT);
  QVERIFY(m_gpu != nullptr);
  QVERIFY(m_npu != nullptr);
  QCOMPARE(m_gpu->name(), QString("GPU 0"));
  QCOMPARE(m_npu->name(), QString("Tensor Unit 0"));

  // Sanity-check the exported symbols the example programs rely on.
  const auto &abs = IOManager::get().assemblerSymbols().abs;
  auto hasSymbol = [&](const QString &name) {
    for (const auto &kv : abs)
      if (kv.first.v == name)
        return true;
    return false;
  };
  QVERIFY(hasSymbol("GPU_0_BASE"));
  QVERIFY(hasSymbol("GPU_0_CTRL_OFFSET"));
  QVERIFY(hasSymbol("GPU_0_STATUS_DONE"));
  QVERIFY(hasSymbol("TENSOR_UNIT_0_TAIL"));
  QVERIFY(hasSymbol("TENSOR_UNIT_0_OP_MATMUL"));

  // Capture everything the program prints through the ecall interface. The
  // syscalls execute on a worker thread which the handler waits on, so a
  // direct connection is safe and keeps the text in program order. The
  // print-string syscall includes the terminating NUL in the emitted text;
  // drop it so the captured console is a plain string.
  connect(
      &SystemIO::get(), &SystemIO::doPrint, this,
      [this](const QString &text) {
        QString t = text;
        t.remove(QChar('\0'));
        m_console += t;
      },
      Qt::DirectConnection);
}

void tst_AccelE2E::cleanup() {
  SystemIO::reset();
  m_console.clear();
}

AInt tst_AccelE2E::baseOf(IOBase *p) const {
  for (const auto &kv : IOManager::get().memoryMap())
    if (kv.second.name == p->name())
      return kv.second.startAddr;
  return 0;
}

void tst_AccelE2E::loadAssemblySource(const QString &src) {
  auto res = ProcessorHandler::getAssembler()->assembleRaw(
      src, &IOManager::get().assemblerSymbols());
  if (res.errors.size() != 0) {
    QString err = "Could not assemble program:\n" + res.errors.toString();
    QFAIL(err.toStdString().c_str());
  }
  ProcessorHandler::loadProgram(std::make_shared<Program>(res.program));
  // Resets the processor and (through ProcessorHandler::_reset) all IO
  // devices.
  RipesSettings::getObserver(RIPES_GLOBALSIGNAL_REQRESET)->trigger();
  m_console.clear();
}

void tst_AccelE2E::loadAssembly(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly)) {
    QFAIL(("Could not open " + path).toStdString().c_str());
  }
  loadAssemblySource(QString::fromUtf8(f.readAll()));
}

unsigned tst_AccelE2E::runToCompletion(bool &timedOut) {
  timedOut = false;
  QElapsedTimer wall;
  wall.start();
  unsigned cycles = 0;
  auto *proc = ProcessorHandler::getProcessorNonConst();
  while (!proc->finished()) {
    proc->clock();
    cycles++;
    if (cycles >= s_maxCycles || wall.elapsed() > s_maxWallMs) {
      timedOut = true;
      break;
    }
  }
  return cycles;
}

void tst_AccelE2E::testGpuVecAdd() {
  loadAssembly(s_examplesDir + "/assembly/gpu_vecadd.s");

  bool timedOut = false;
  const unsigned cycles = runToCompletion(timedOut);
  const std::string console = m_console.toStdString();
  QVERIFY2(!timedOut, ("gpu_vecadd.s did not finish; console:\n" + console)
                          .c_str());
  QVERIFY(ProcessorHandler::getProcessor()->finished());
  QVERIFY2(m_console.contains("Program exited with code: 0"),
           ("program did not exit via ecall; console:\n" + console).c_str());
  QVERIFY2(m_console.contains("Result OK: C[i] = A[i] + B[i] for all i"),
           ("GPU result mismatch; console:\n" + console).c_str());
  QVERIFY(!m_console.contains("MISMATCH"));
  QVERIFY(!m_console.contains("GPU reported an error"));
  QVERIFY(m_console.contains("GPU done in "));
  qInfo() << "gpu_vecadd.s finished in" << cycles << "CPU cycles";
  qInfo().noquote() << m_console;

  // After the program acknowledged DONE (W1C) the GPU is idle again.
  auto *gpu = dynamic_cast<IOGpu *>(m_gpu);
  QVERIFY(gpu != nullptr);
  const auto status = gpu->ioRead(IOGpu::STATUS * 4, 4);
  QCOMPARE(status & IOGpu::STATUS_BUSY, VInt(0));
  QCOMPARE(status & IOGpu::STATUS_DONE, VInt(0));
  QCOMPARE(status & IOGpu::STATUS_ERROR, VInt(0));
  QVERIFY(gpu->ioRead(IOGpu::CYCLES * 4, 4) > 0);
  QVERIFY(gpu->ioRead(IOGpu::STATS_INSTRS * 4, 4) > 0);
}

void tst_AccelE2E::testNpuMatmul() {
  loadAssembly(s_examplesDir + "/assembly/npu_matmul.s");

  bool timedOut = false;
  const unsigned cycles = runToCompletion(timedOut);
  const std::string console = m_console.toStdString();
  QVERIFY2(!timedOut, ("npu_matmul.s did not finish; console:\n" + console)
                          .c_str());
  QVERIFY(ProcessorHandler::getProcessor()->finished());
  QVERIFY2(m_console.contains("Program exited with code: 0"),
           ("program did not exit via ecall; console:\n" + console).c_str());
  QVERIFY2(m_console.contains("Result: OK"),
           ("NPU result mismatch; console:\n" + console).c_str());
  QVERIFY(!m_console.contains("MISMATCH"));
  QVERIFY(m_console.contains("NPU MACs:"));
  qInfo() << "npu_matmul.s finished in" << cycles << "CPU cycles";
  qInfo().noquote() << m_console;

  // 4x4x4 matmul = 64 MACs. The DMA engine moved the 32-byte descriptor,
  // A (16 B int8), B (16 B int8) and C (64 B int32) = 128 bytes.
  const AInt base = baseOf(m_npu);
  QVERIFY(base != 0);
  auto &mem = ProcessorHandler::getMemory();
  QCOMPARE(mem.readMem(base + IOTensorUnit::MACS * 4, 4), VInt(64));
  QCOMPARE(mem.readMem(base + IOTensorUnit::DMA_BYTES * 4, 4),
           VInt(npu::kDescriptorBytes + 16 + 16 + 64));
  QCOMPARE(mem.readMem(base + IOTensorUnit::HEAD * 4, 4), VInt(1));
  QCOMPARE(mem.readMem(base + IOTensorUnit::TAIL * 4, 4), VInt(1));
  QCOMPARE(mem.readMem(base + IOTensorUnit::STATUS * 4, 4) &
               VInt(npu::Status::BUSY | npu::Status::ERROR),
           VInt(0));
}

void tst_AccelE2E::testGpuRegisterLevel() {
  // A program that never finishes, so the processor can be clocked freely
  // while the GPU runs. Kernel and data live in .data so they are part of the
  // loaded program image.
  loadAssemblySource(QString(R"(
.data
.align 4
kernel:
%1A:  .word 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13
B:  .word 10, 20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130
C:  .word 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
.text
main:
    nop
spin:
    j spin
)").arg(kernelWords(kVecAddSource)));

  auto *gpu = dynamic_cast<IOGpu *>(m_gpu);
  QVERIFY(gpu != nullptr);
  const AInt base = baseOf(m_gpu);
  QVERIFY(base != 0);
  auto &mem = ProcessorHandler::getMemory();

  // Resolve the data symbols from the loaded program.
  const auto &program = ProcessorHandler::getProgram();
  QVERIFY(program != nullptr);
  auto addrOf = [&](const QString &name) -> AInt {
    for (const auto &kv : program->symbols)
      if (kv.second.v == name)
        return kv.first;
    return 0;
  };
  const AInt kernel = addrOf("kernel");
  const AInt a = addrOf("A");
  const AInt b = addrOf("B");
  const AInt c = addrOf("C");
  QVERIFY(kernel != 0 && a != 0 && b != 0 && c != 0);
  constexpr uint32_t n = 13; // ragged: 2 blocks of 8, last one half full

  // Idle after reset.
  QCOMPARE(mem.readMem(base + IOGpu::STATUS * 4, 4), VInt(0));
  const VInt config = mem.readMem(base + IOGpu::CONFIG * 4, 4);
  const unsigned warpSize = config & 0xFF;
  QVERIFY(warpSize >= 1);

  // Write the launch registers through the processor memory interface (the IO
  // region installed by the IOManager).
  auto wr = [&](unsigned reg, VInt v) { mem.writeMem(base + reg * 4, v, 4); };
  auto rd = [&](unsigned reg) { return mem.readMem(base + reg * 4, 4); };
  wr(IOGpu::KERNEL_PC, kernel);
  wr(IOGpu::GRID_X, 2);
  wr(IOGpu::BLOCK_X, warpSize);
  wr(IOGpu::ARG0, a);
  wr(IOGpu::ARG1, b);
  wr(IOGpu::ARG2, c);
  wr(IOGpu::ARG3, n);
  QCOMPARE(rd(IOGpu::KERNEL_PC), kernel);
  QCOMPARE(rd(IOGpu::GRID_X), VInt(2));
  QCOMPARE(rd(IOGpu::ARG3), VInt(n));

  // Doorbell.
  wr(IOGpu::CTRL, IOGpu::CTRL_START);
  VInt status = rd(IOGpu::STATUS);
  QVERIFY2((status & IOGpu::STATUS_BUSY) != 0, "GPU not busy after START");
  QCOMPARE(status & IOGpu::STATUS_DONE, VInt(0));

  // Step the GPU by clocking the processor (processorClocked -> IOGpu).
  auto *proc = ProcessorHandler::getProcessorNonConst();
  unsigned cycles = 0;
  while (!(rd(IOGpu::STATUS) & IOGpu::STATUS_DONE) && cycles < 10000) {
    proc->clock();
    cycles++;
  }
  status = rd(IOGpu::STATUS);
  QVERIFY2((status & IOGpu::STATUS_DONE) != 0,
           "STATUS.DONE did not go high within 10000 cycles");
  QCOMPARE(status & IOGpu::STATUS_BUSY, VInt(0));
  QCOMPARE(status & IOGpu::STATUS_ERROR, VInt(0));
  QVERIFY(cycles > 0);
  QVERIFY(rd(IOGpu::CYCLES) > 0);
  QVERIFY(rd(IOGpu::CYCLES) <= cycles * 64); // bounded by steps/cycle param
  qInfo() << "register-level vecadd: DONE after" << cycles << "CPU cycles,"
          << rd(IOGpu::CYCLES) << "GPU cycles," << rd(IOGpu::STATS_INSTRS)
          << "warp instructions," << rd(IOGpu::STATS_MEMTXN)
          << "memory transactions";

  // The kernel wrote C[i] = A[i] + B[i] via the peripheral's memWrite hook.
  for (uint32_t i = 0; i < n; i++) {
    const VInt expect = mem.readMem(a + i * 4, 4) + mem.readMem(b + i * 4, 4);
    QCOMPARE(mem.readMem(c + i * 4, 4), expect);
  }

  // W1C: acknowledging DONE clears it; nothing else changes.
  wr(IOGpu::STATUS, IOGpu::STATUS_DONE);
  QCOMPARE(rd(IOGpu::STATUS), VInt(0));
  QCOMPARE(rd(IOGpu::ARG3), VInt(n));

  // IRQ_EN: DONE sets the IRQ bit when enabled.
  wr(IOGpu::CTRL, IOGpu::CTRL_IRQ_EN | IOGpu::CTRL_START);
  cycles = 0;
  while (!(rd(IOGpu::STATUS) & IOGpu::STATUS_DONE) && cycles < 10000) {
    proc->clock();
    cycles++;
  }
  status = rd(IOGpu::STATUS);
  QVERIFY((status & IOGpu::STATUS_DONE) != 0);
  QVERIFY((status & IOGpu::STATUS_IRQ) != 0);
  wr(IOGpu::STATUS, IOGpu::STATUS_DONE);
  QCOMPARE(rd(IOGpu::STATUS) & (IOGpu::STATUS_DONE | IOGpu::STATUS_IRQ),
           VInt(0));
}

AInt tst_AccelE2E::symbol(const QString &name) const {
  const auto &program = ProcessorHandler::getProgram();
  if (!program)
    return 0;
  for (const auto &kv : program->symbols)
    if (kv.second.v == name)
      return kv.first;
  return 0;
}

void tst_AccelE2E::setupVecAdd(uint32_t n, uint32_t block) {
  const AInt base = baseOf(m_gpu);
  auto &mem = ProcessorHandler::getMemory();
  auto wr = [&](unsigned reg, VInt v) { mem.writeMem(base + reg * 4, v, 4); };
  wr(IOGpu::KERNEL_PC, symbol("kernel"));
  wr(IOGpu::GRID_X, (n + block - 1) / block);
  wr(IOGpu::BLOCK_X, block);
  wr(IOGpu::ARG0, symbol("A"));
  wr(IOGpu::ARG1, symbol("B"));
  wr(IOGpu::ARG2, symbol("C"));
  wr(IOGpu::ARG3, n);
}

unsigned tst_AccelE2E::clockUntilDone(unsigned maxCycles) {
  const AInt base = baseOf(m_gpu);
  auto &mem = ProcessorHandler::getMemory();
  auto *proc = ProcessorHandler::getProcessorNonConst();
  unsigned cycles = 0;
  while (!(mem.readMem(base + IOGpu::STATUS * 4, 4) & IOGpu::STATUS_DONE) &&
         cycles < maxCycles) {
    proc->clock();
    cycles++;
  }
  return cycles;
}

static QString vecAddData(unsigned n) {
  QStringList a, b, c;
  for (unsigned i = 0; i < n; ++i) {
    a << QString::number(i + 1);
    b << QString::number(10 * (i + 1));
    c << "0";
  }
  return "A:  .word " + a.join(", ") + "\nB:  .word " + b.join(", ") +
         "\nC:  .word " + c.join(", ") + "\n";
}

void tst_AccelE2E::testGpuExampleMatchesSource() {
  // The example host program embeds pre-assembled kernel words; they must be
  // exactly what the kernel assembler produces for the vecadd source.
  QFile f(s_examplesDir + "/assembly/gpu_vecadd.s");
  QVERIFY(f.open(QIODevice::ReadOnly));
  const QStringList lines = QString::fromUtf8(f.readAll()).split('\n');
  QRegularExpression re("^\\s*\\.word\\s+0x([0-9a-fA-F]+)\\s*#");
  std::vector<uint32_t> words;
  bool inKernel = false;
  for (const auto &line : lines) {
    if (line.startsWith("kernel:")) {
      inKernel = true;
      continue;
    }
    if (!inKernel)
      continue;
    const auto m = re.match(line);
    if (!m.hasMatch())
      break;
    words.push_back(m.captured(1).toUInt(nullptr, 16));
  }
  const auto expected = gpu::assembleKernel(kVecAddSource);
  QCOMPARE(words.size(), expected.size());
  for (size_t i = 0; i < words.size(); ++i)
    QCOMPARE(words[i], expected[i]);
}

void tst_AccelE2E::testGpuSelfAccessNoDeadlock() {
  // Regression test for the IOGpu deadlock: a kernel load (and a store) that
  // lands in the GPU's own register window used to re-enter ioRead/ioWrite
  // while the non-recursive mutex was held. Now the access is refused and
  // the kernel stops with STATUS.ERROR. Clocking runs on a worker thread so
  // that a regression shows up as a timeout instead of a hung test.
  loadAssemblySource(spinProgram(
      kernelWords("S_ARG R1, 0\nLDG R2, [R1+4]\nSTG [R1+0], R2\nEXIT\n"),
      QString()));
  const AInt base = baseOf(m_gpu);
  auto &mem = ProcessorHandler::getMemory();
  auto wr = [&](unsigned reg, VInt v) { mem.writeMem(base + reg * 4, v, 4); };
  auto rd = [&](unsigned reg) { return mem.readMem(base + reg * 4, 4); };
  wr(IOGpu::KERNEL_PC, symbol("kernel"));
  wr(IOGpu::GRID_X, 1);
  wr(IOGpu::BLOCK_X, 4);
  wr(IOGpu::ARG0, base); // pointer into the GPU's own MMIO window
  wr(IOGpu::CTRL, IOGpu::CTRL_START);

  std::atomic<bool> finished{false};
  std::atomic<unsigned> cycles{0};
  std::thread worker([&] {
    auto *proc = ProcessorHandler::getProcessorNonConst();
    while (cycles < 2000) {
      proc->clock();
      cycles++;
      if (rd(IOGpu::STATUS) & IOGpu::STATUS_DONE)
        break;
    }
    finished = true;
  });
  QElapsedTimer t;
  t.start();
  while (!finished && t.elapsed() < 15000)
    std::this_thread::yield();
  if (!finished) {
    qCritical("GPU self-access deadlocked (no progress for 15 s)");
    std::_Exit(1); // the worker is stuck; it cannot be joined
  }
  worker.join();

  const VInt status = rd(IOGpu::STATUS);
  QVERIFY2(status & IOGpu::STATUS_DONE, "kernel did not finish");
  QVERIFY2(status & IOGpu::STATUS_ERROR, "self access not reported as ERROR");
  QCOMPARE(status & IOGpu::STATUS_BUSY, VInt(0));
  const auto v = static_cast<IOGpu *>(m_gpu)->view();
  QVERIFY2(v.errorMessage.contains("own register window"),
           v.errorMessage.toStdString().c_str());
  qInfo() << "self access reported after" << cycles.load()
          << "CPU cycles:" << v.errorMessage;
  // KERNEL_PC was not overwritten by the refused STG.
  QCOMPARE(rd(IOGpu::KERNEL_PC), symbol("kernel"));
  // The peripheral still works afterwards.
  wr(IOGpu::STATUS, IOGpu::STATUS_DONE | IOGpu::STATUS_ERROR);
  QCOMPARE(rd(IOGpu::STATUS), VInt(0));
}

void tst_AccelE2E::testGpuLaunchWhileBusy() {
  constexpr unsigned n = 40;
  loadAssemblySource(spinProgram(kernelWords(kVecAddSource), vecAddData(n)));
  const AInt base = baseOf(m_gpu);
  auto &mem = ProcessorHandler::getMemory();
  auto wr = [&](unsigned reg, VInt v) { mem.writeMem(base + reg * 4, v, 4); };
  auto rd = [&](unsigned reg) { return mem.readMem(base + reg * 4, 4); };
  setupVecAdd(n, 8);
  wr(IOGpu::CTRL, IOGpu::CTRL_START);
  auto *proc = ProcessorHandler::getProcessorNonConst();
  for (int i = 0; i < 5; ++i)
    proc->clock();
  QVERIFY(rd(IOGpu::STATUS) & IOGpu::STATUS_BUSY);

  // Doorbell again while BUSY, with a block size that would be invalid.
  wr(IOGpu::BLOCK_X, 4096);
  wr(IOGpu::CTRL, IOGpu::CTRL_START);
  VInt status = rd(IOGpu::STATUS);
  QVERIFY(status & IOGpu::STATUS_BUSY);
  QVERIFY(status & IOGpu::STATUS_LAUNCH_IGNORED);
  QCOMPARE(status & IOGpu::STATUS_ERROR, VInt(0));

  clockUntilDone(20000);
  status = rd(IOGpu::STATUS);
  QVERIFY(status & IOGpu::STATUS_DONE);
  QCOMPARE(status & IOGpu::STATUS_ERROR, VInt(0)); // not poisoned
  const AInt c = symbol("C");
  for (unsigned i = 0; i < n; ++i)
    QCOMPARE(mem.readMem(c + i * 4, 4), VInt(11 * (i + 1)));
  QCOMPARE(static_cast<IOGpu *>(m_gpu)->view().ignoredLaunches, uint64_t(1));
  // W1C clears the sticky LAUNCH_IGNORED bit.
  wr(IOGpu::STATUS, IOGpu::STATUS_LAUNCH_IGNORED | IOGpu::STATUS_DONE);
  QCOMPARE(rd(IOGpu::STATUS), VInt(0));
}

void tst_AccelE2E::testGpuReverse() {
  constexpr unsigned n = 24;
  loadAssemblySource(spinProgram(kernelWords(kVecAddSource), vecAddData(n)));
  auto *gpu = static_cast<IOGpu *>(m_gpu);
  const AInt base = baseOf(m_gpu);
  auto &mem = ProcessorHandler::getMemory();
  auto wr = [&](unsigned reg, VInt v) { mem.writeMem(base + reg * 4, v, 4); };
  auto rd = [&](unsigned reg) { return mem.readMem(base + reg * 4, 4); };
  auto *proc = ProcessorHandler::getProcessorNonConst();
  setupVecAdd(n, 8);
  wr(IOGpu::CTRL, IOGpu::CTRL_START);

  for (int i = 0; i < 10; ++i)
    proc->clock();
  const VInt cyc10 = rd(IOGpu::CYCLES);
  const VInt instr10 = rd(IOGpu::STATS_INSTRS);
  QVERIFY(cyc10 > 0);
  for (int i = 0; i < 6; ++i)
    proc->clock();
  QVERIFY(rd(IOGpu::CYCLES) > cyc10);
  for (int i = 0; i < 6; ++i)
    proc->reverseProcessor();
  QCOMPARE(rd(IOGpu::CYCLES), cyc10);
  QCOMPARE(rd(IOGpu::STATS_INSTRS), instr10);
  QVERIFY(rd(IOGpu::STATUS) & IOGpu::STATUS_BUSY);
  QCOMPARE(gpu->view().notice, IOGpu::Notice::ReverseRestored);
  qInfo() << "reverse-execution frame size:" << gpu->currentFrameBytes()
          << "bytes; history:" << gpu->view().historyFrames << "frames,"
          << gpu->view().historyBytes << "bytes";

  // Re-executing after the reverse produces the same, correct result.
  clockUntilDone(20000);
  QVERIFY(rd(IOGpu::STATUS) & IOGpu::STATUS_DONE);
  QCOMPARE(rd(IOGpu::STATUS) & IOGpu::STATUS_ERROR, VInt(0));
  const AInt c = symbol("C");
  for (unsigned i = 0; i < n; ++i)
    QCOMPARE(mem.readMem(c + i * 4, 4), VInt(11 * (i + 1)));

  // Reversing past the recorded history falls back to the pre-launch state.
  loadAssemblySource(spinProgram(kernelWords(kVecAddSource), vecAddData(n)));
  setupVecAdd(n, 8);
  wr(IOGpu::CTRL, IOGpu::CTRL_START);
  QVERIFY(rd(IOGpu::STATUS) & IOGpu::STATUS_BUSY);
  emit ProcessorHandler::get()->processorReversed(); // no frame to pop
  QCOMPARE(rd(IOGpu::STATUS), VInt(0));
  QCOMPARE(rd(IOGpu::KERNEL_PC), symbol("kernel")); // launch regs kept
  QCOMPARE(gpu->view().notice, IOGpu::Notice::ReverseReset);
}

void tst_AccelE2E::testGpuWidgetRender() {
  // Offscreen rendering of the redesigned widget in the middle of a kernel
  // (a divergent kernel so that the divergence highlight is visible).
  const char *divergent = R"(
        S_TID R1
        ANDI R2, R1, 1
        SETP.NE P0, R2, 0
        @P0 BRA odd
        S_ARG R6, 0
        LDG R7, [R6]
        LDG R7, [R6+32]
        BRA join
    odd:
        MOV R3, 200
    join:
        S_ARG R4, 0
        SHL R5, R1, 2
        ADD R4, R4, R5
        STG [R4], R1
        EXIT
  )";
  loadAssemblySource(spinProgram(kernelWords(divergent), vecAddData(48)));
  auto *gpu = static_cast<IOGpu *>(m_gpu);
  auto *widget = gpu->findChild<IOGpuWidget *>();
  QVERIFY(widget != nullptr);
  gpu->resize(1000, 720);
  gpu->show();
  QCoreApplication::processEvents();
  widget->refresh(true);
  QPixmap empty = gpu->grab();
  QVERIFY(!empty.isNull());

  const AInt base = baseOf(m_gpu);
  auto &mem = ProcessorHandler::getMemory();
  auto wr = [&](unsigned reg, VInt v) { mem.writeMem(base + reg * 4, v, 4); };
  wr(IOGpu::KERNEL_PC, symbol("kernel"));
  wr(IOGpu::GRID_X, 3);
  wr(IOGpu::BLOCK_X, 12);
  wr(IOGpu::ARG0, symbol("C"));
  wr(IOGpu::CTRL, IOGpu::CTRL_START);
  auto *proc = ProcessorHandler::getProcessorNonConst();
  for (int i = 0; i < 9; ++i)
    proc->clock();
  QCoreApplication::processEvents();
  widget->refresh(true);
  QPixmap running = gpu->grab();
  QVERIFY(!running.isNull());

  const QString dir = qEnvironmentVariableIsSet("GPU_WIDGET_PNG_DIR")
                          ? qEnvironmentVariable("GPU_WIDGET_PNG_DIR")
                          : QDir::tempPath();
  QVERIFY(empty.save(dir + "/gpu_widget_empty.png"));
  QVERIFY(running.save(dir + "/gpu_widget_running.png"));
  for (auto *w : gpu->findChildren<QWidget *>())
    if (w->isVisible() && w->minimumSizeHint().width() > 600)
      qInfo() << "wide child" << w->metaObject()->className()
              << w->objectName() << w->minimumSizeHint();
  // Nothing forces the widget wider than 1000 px (1280x800 main window).
  QCOMPARE(running.size(), QSize(1000, 720));
  clockUntilDone(20000);
  widget->refresh(true);
  QVERIFY(gpu->grab().save(dir + "/gpu_widget_done.png"));
  qInfo() << "widget screenshots written to" << dir;
  gpu->hide();
}

void tst_AccelE2E::testGpuExercises() {
  // The user-guide exercises (docs/guide/*/09-gpu.md) must assemble and run.
  bool timedOut = false;

  loadAssembly(s_examplesDir + "/assembly/exercise_gpu_saxpy.s");
  unsigned cycles = runToCompletion(timedOut);
  std::string console = m_console.toStdString();
  QVERIFY2(!timedOut, ("saxpy did not finish; console:\n" + console).c_str());
  QVERIFY2(m_console.contains("Program exited with code: 0"), console.c_str());
  QVERIFY2(m_console.contains("Result OK"), console.c_str());
  QVERIFY(!m_console.contains("MISMATCH"));
  QVERIFY(!m_console.contains("GPU reported an error"));
  qInfo() << "exercise_gpu_saxpy.s:" << cycles << "CPU cycles;"
          << m_console.trimmed();
  m_console.clear();

  loadAssembly(s_examplesDir + "/assembly/exercise_gpu_divergence.s");
  cycles = runToCompletion(timedOut);
  console = m_console.toStdString();
  QVERIFY2(!timedOut,
           ("divergence did not finish; console:\n" + console).c_str());
  QVERIFY2(m_console.contains("Program exited with code: 0"), console.c_str());
  QCOMPARE(m_console.count("result OK"), 2);
  QVERIFY(!m_console.contains("MISMATCH"));
  qInfo().noquote() << "exercise_gpu_divergence.s:" << cycles
                    << "CPU cycles\n" << m_console.trimmed();
  QRegularExpression re("instrs=(\\d+) divergences=(\\d+)");
  std::vector<std::pair<int, int>> counts;
  for (auto it = re.globalMatch(m_console); it.hasNext();) {
    const auto m = it.next();
    counts.push_back({m.captured(1).toInt(), m.captured(2).toInt()});
  }
  QCOMPARE(counts.size(), size_t(2));
  // Branch kernel: 7 common + SUB on the negative lanes + STG + EXIT = 10
  // warp instructions, 1 divergence. Predicated kernel: 9, 0.
  QCOMPARE(counts[0].first, 10);
  QCOMPARE(counts[0].second, 1);
  QCOMPARE(counts[1].first, 9);
  QCOMPARE(counts[1].second, 0);
}

void tst_AccelE2E::testMemoryMapRangeText() {
  // A zero-sized entry (e.g. an empty .data section) must not be shown as a
  // backwards range such as "0x10000000 - 0x0fffffff".
  QCOMPARE(MemoryMapModel::rangeText(0x10000000, 0, 4),
           QString("0x10000000 (empty)"));
  QCOMPARE(MemoryMapModel::rangeText(0x10000000, 0x10, 4),
           QString("0x10000000 - 0x1000000f"));
  QCOMPARE(MemoryMapModel::rangeText(0xf0000000, 1, 4),
           QString("0xf0000000 - 0xf0000000"));
}

void tst_AccelE2E::runCacheAccesses(const std::shared_ptr<CacheSim> &cache,
                                    unsigned cycles) {
  auto *proc = ProcessorHandler::getProcessorNonConst();
  for (unsigned i = 0; i < cycles; ++i) {
    proc->clock();
    cache->access(0x10000000 + (i % 256) * 4, MemoryAccess::Read);
  }
  emit ProcessorHandler::get()->runFinished();
}

void tst_AccelE2E::testCachePlotWindow() {
  // A run longer than "Max. cache plot cycles" used to leave the plot empty
  // (the plot only took cycles below the limit, while the trace keeps the
  // most recent ones). It must show the most recent window instead and say
  // that older cycles were dropped.
  RipesSettings::setValue(RIPES_SETTING_CACHE_MAXCYCLES, 300);
  RipesSettings::setValue(RIPES_SETTING_CACHE_MAXTRACES, 100000);
  loadAssemblySource("main:\n  addi a0, a0, 1\n  j main\n");

  auto cache = std::make_shared<CacheSim>(nullptr);
  CachePlotWidget plot;
  plot.setCache(cache);
  QCOMPARE(plot.plottedPointCount(), 0);
  QVERIFY(!plot.isPlotTruncated());

  runCacheAccesses(cache, 2000);
  QVERIFY(!cache->isAccessTraceTruncated());
  QVERIFY2(plot.plottedPointCount() > 0, "plot is empty after a long run");
  const auto [first, last] = plot.plottedCycleRange();
  QVERIFY2(last >= 1990, qPrintable(QString("last plotted cycle %1").arg(last)));
  QVERIFY2(first > 0 && last - first <= 300,
           qPrintable(QString("plotted %1 - %2").arg(first).arg(last)));
  QVERIFY(plot.isPlotTruncated());
  QVERIFY2(plot.truncationHintText().contains("Max. cache plot cycles"),
           qPrintable(plot.truncationHintText()));

  // A short run fits the window: no hint.
  loadAssemblySource("main:\n  addi a0, a0, 1\n  j main\n");
  cache->reset();
  CachePlotWidget plot2;
  plot2.setCache(cache);
  runCacheAccesses(cache, 100);
  QVERIFY(plot2.plottedPointCount() > 0);
  QVERIFY(!plot2.isPlotTruncated());
  QVERIFY(plot2.truncationHintText().isEmpty());

  RipesSettings::setValue(RIPES_SETTING_CACHE_MAXCYCLES, 10000);
  RipesSettings::setValue(RIPES_SETTING_CACHE_MAXTRACES, 10000);
}

void tst_AccelE2E::testCachePlotTraceTruncated() {
  // The cache drops old trace entries beyond "Max. cache access traces";
  // the plot shows what is left and says that older cycles were dropped.
  RipesSettings::setValue(RIPES_SETTING_CACHE_MAXCYCLES, 100000);
  RipesSettings::setValue(RIPES_SETTING_CACHE_MAXTRACES, 400);
  loadAssemblySource("main:\n  addi a0, a0, 1\n  j main\n");

  auto cache = std::make_shared<CacheSim>(nullptr);
  CachePlotWidget plot;
  plot.setCache(cache);
  runCacheAccesses(cache, 3000);
  QVERIFY(cache->isAccessTraceTruncated());
  QVERIFY2(plot.plottedPointCount() > 0, "plot is empty after a long run");
  QVERIFY(plot.plottedCycleRange().first > 0);
  QVERIFY(plot.isPlotTruncated());
  QVERIFY(!plot.truncationHintText().isEmpty());
  // Cumulative statistics still cover the whole run.
  QCOMPARE(cache->getHits() + cache->getMisses(), 3000u);

  RipesSettings::setValue(RIPES_SETTING_CACHE_MAXCYCLES, 10000);
  RipesSettings::setValue(RIPES_SETTING_CACHE_MAXTRACES, 10000);
}

QTEST_MAIN(tst_AccelE2E)
#include "tst_accel_e2e.moc"

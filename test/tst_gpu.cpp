#include <QtTest/QTest>

#include <map>
#include <string>
#include <vector>

#include "accel/gpu/simtgpu.h"

using namespace Ripes::gpu;

namespace {

/// Minimal word-addressed memory to back the model's bus callbacks.
struct TestMemory {
  std::map<uint32_t, uint32_t> words;
  uint32_t read(uint32_t addr) {
    auto it = words.find(addr & ~3u);
    return it == words.end() ? 0u : it->second;
  }
  void write(uint32_t addr, uint32_t value) { words[addr & ~3u] = value; }
  void load(uint32_t base, const std::vector<uint32_t> &program) {
    for (size_t i = 0; i < program.size(); ++i)
      write(base + static_cast<uint32_t>(i) * 4, program[i]);
  }
};

SimtGpu makeGpu(const GpuConfig &cfg, TestMemory &mem) {
  return SimtGpu(
      cfg, [&mem](uint32_t a) { return mem.read(a); },
      [&mem](uint32_t a, uint32_t v) { mem.write(a, v); });
}

uint64_t runToCompletion(SimtGpu &gpu, uint64_t maxCycles = 200000) {
  uint64_t cycles = 0;
  while (gpu.busy() && cycles < maxCycles) {
    gpu.step();
    ++cycles;
  }
  return cycles;
}

const char *const kVecAdd = R"(
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

} // namespace

class tst_Gpu : public QObject {
  Q_OBJECT

private slots:
  void tst_encoding();
  void tst_assemblerRoundTrip();
  void tst_assemblerErrors();
  void tst_vecadd();
  void tst_divergenceReconvergence();
  void tst_loopDivergence();
  void tst_barrier();
  void tst_coalescing();
  void tst_latencyHiding();
  void tst_launchErrors();
  void tst_opcodeTable();
  void tst_divergentBarrier();
  void tst_barrierWithExitedPath();
  void tst_launchWhileBusy();
  void tst_busFault();
  void tst_coalescingStats();
  void tst_stateSnapshot();
};

void tst_Gpu::tst_encoding() {
  // Hand-computed reference encodings (see docs/arch/gpu.md).
  QCOMPARE(encodeI(Opcode::MOVS, 1, 0, 0), 0x01040000u);         // S_TID R1
  QCOMPARE(encodeR(Opcode::MUL, 4, 2, 3), 0x02908C00u);           // MUL R4,R2,R3
  QCOMPARE(encodeR(Opcode::SETP, 0, 4, 5, 0, 3), 0x08011403u);    // SETP.GE
  QCOMPARE(encodeR(Opcode::EXIT, 0, 0, 0, 0, 0, makeGuard(true, false, 0)),
           0x80400000u);                                          // @P0 EXIT
  QCOMPARE(encodeI(Opcode::LDG, 9, 6, 0), 0x0A258000u);           // LDG R9,[R6]
  QCOMPARE(encodeI(Opcode::STG, 11, 8, 0), 0x0A6E0000u);          // STG [R8],R11
  QCOMPARE(encodeP(0, 1, Cond::LT, 4), 0x08404804u);              // SETP.LT P0,R1,4
  QCOMPARE(encodeB(4, makeGuard(true, false, 0)), 0x80C00004u);   // @P0 BRA 4
  QCOMPARE(encodeI(Opcode::ADDI, 2, 0, -5), 0x06083FFBu);         // MOV R2,-5

  const Instr in = decode(0x08404804u);
  QVERIFY(in.valid);
  QCOMPARE(static_cast<int>(in.op), static_cast<int>(Opcode::SETPI));
  QCOMPARE(static_cast<int>(in.rs1), 1);
  QCOMPARE(static_cast<int>(in.rs2 & 7u), static_cast<int>(Cond::LT));
  QCOMPARE(in.imm, 4);
  QVERIFY(!decode(0xFFFFFFFFu).valid);

  QCOMPARE(QString::fromStdString(disassemble(encodeI(Opcode::LDG, 5, 4, -4))),
           QString("LDG R5, [R4-4]"));
  QCOMPARE(QString::fromStdString(disassemble(
               encodeP(1, 3, Cond::LTU, -1, makeGuard(true, true, 2)))),
           QString("@!P2 SETP.LTU P1, R3, -1"));
}

void tst_Gpu::tst_assemblerRoundTrip() {
  std::string err;
  const auto words = assembleKernel(kVecAdd, &err);
  QVERIFY2(err.empty(), err.c_str());
  QCOMPARE(words.size(), size_t(20));
  QCOMPARE(words[7], 0x80400000u);
  QCOMPARE(words[8], 0x07510002u); // SHL with immediate -> SHLI

  std::string src;
  for (const auto w : words)
    src += disassemble(w) + "\n";
  const auto again = assembleKernel(src, &err);
  QVERIFY2(err.empty(), err.c_str());
  QVERIFY(again == words);

  // Labels, guards and both store operand orders.
  const auto prog = assembleKernel("S_TID R1\nSETP.LT P0, R1, 4\n@P0 BRA "
                                   "skip\nMOV R2, -5\nskip: STG R2, "
                                   "[R3+8]\nEXIT\n",
                                   &err);
  QVERIFY2(err.empty(), err.c_str());
  QCOMPARE(prog.size(), size_t(6));
  QCOMPARE(prog[2], 0x80C00004u);
  QCOMPARE(prog[3], 0x06083FFBu);
  QCOMPARE(prog[4], 0x0A48C008u);
}

void tst_Gpu::tst_assemblerErrors() {
  std::string err;
  QVERIFY(assembleKernel("ADD R1, R2\n", &err).empty());
  QVERIFY(!err.empty());
  QVERIFY(assembleKernel("BRA nowhere\n", &err).empty());
  QVERIFY(assembleKernel("SETP P0, R1, R2\n", &err).empty());
  QVERIFY(assembleKernel("ADDI R1, R2, 100000\n", &err).empty());
  QVERIFY(assembleKernel("FOO R1\n", &err).empty());
  bool threw = false;
  try {
    assembleKernel("ADD R1, R2\n");
  } catch (const std::runtime_error &) {
    threw = true;
  }
  QVERIFY(threw);
}

void tst_Gpu::tst_vecadd() {
  TestMemory mem;
  GpuConfig cfg; // 2 SM x 4 warps x 8 lanes
  SimtGpu gpu = makeGpu(cfg, mem);

  const uint32_t kpc = 0x1000, A = 0x2000, B = 0x3000, C = 0x4000, N = 21;
  mem.load(kpc, assembleKernel(kVecAdd));
  for (uint32_t i = 0; i < N; ++i) {
    mem.write(A + i * 4, i);
    mem.write(B + i * 4, 100 * i);
  }
  LaunchParams lp;
  lp.kernelPc = kpc;
  lp.blockX = 16;
  lp.gridX = (N + 15) / 16; // 2 blocks, the last one partially filled
  lp.args = {{A, B, C, N}};
  QVERIFY(gpu.launch(lp));
  QVERIFY(gpu.busy());
  runToCompletion(gpu);
  QVERIFY(gpu.done());
  QVERIFY2(!gpu.hasError(), gpu.errorMessage().c_str());

  for (uint32_t i = 0; i < N; ++i)
    QCOMPARE(mem.read(C + i * 4), 101 * i);
  QCOMPARE(mem.read(C + N * 4), 0u); // tail threads were predicated off

  const auto &st = gpu.stats();
  QCOMPARE(st.blocksLaunched, uint64_t(2));
  QCOMPARE(st.blocksCompleted, uint64_t(2));
  QCOMPARE(st.divergences, uint64_t(0)); // guarded EXIT, no branch
  // Block 0: 2 warps; block 1: threads 16..20 (1 warp) + a warp that exits
  // immediately -> 3 warps x (2 LDG + 1 STG) = 9 memory instructions, each
  // touching a single 32-byte segment.
  QCOMPARE(st.memInstrs, uint64_t(9));
  QCOMPARE(st.memTransactions, uint64_t(9));
  QVERIFY(st.cycles > 0);
  QVERIFY(st.issued > 0);
  QCOMPARE(st.issued + st.idleSlots, st.cycles * cfg.numSMs);
}

void tst_Gpu::tst_divergenceReconvergence() {
  const char *kernel = R"(
        S_TID R1
        ANDI R2, R1, 1
        SETP.NE P0, R2, 0
        @P0 BRA odd
        MOV R3, 100
        BRA join
    odd:
        MOV R3, 200
    join:
        S_ARG R4, 0
        SHL R5, R1, 2
        ADD R4, R4, R5
        STG [R4], R3
        EXIT
    )";
  TestMemory mem;
  GpuConfig cfg;
  cfg.numSMs = 1;
  SimtGpu gpu = makeGpu(cfg, mem);
  mem.load(0x100, assembleKernel(kernel));
  LaunchParams lp;
  lp.kernelPc = 0x100;
  lp.blockX = 8;
  lp.gridX = 1;
  lp.args = {{0x800, 0, 0, 0}};
  QVERIFY(gpu.launch(lp));

  // Record (pc, exec mask) of warp 0 after every cycle.
  std::vector<std::pair<uint32_t, uint32_t>> trace;
  while (gpu.busy()) {
    gpu.step();
    const auto &w = gpu.sms()[0].warps[0];
    if (w.valid && w.hasLastInstr)
      trace.push_back({w.lastPc, w.lastExecMask});
  }
  QVERIFY2(!gpu.hasError(), gpu.errorMessage().c_str());
  for (uint32_t i = 0; i < 8; ++i)
    QCOMPARE(mem.read(0x800 + i * 4), (i & 1) ? 200u : 100u);

  QCOMPARE(gpu.stats().divergences, uint64_t(1));
  // 4 common + 2 (even path) + 1 (odd path) + 5 after the join = 12.
  QCOMPARE(gpu.stats().issued, uint64_t(12));

  // Expected execution order under the min-PC rule. (The final EXIT at pc 11
  // retires the warp within the same cycle, so it is not observable here.)
  const std::vector<std::pair<uint32_t, uint32_t>> expected = {
      {0, 0xFF}, {1, 0xFF}, {2, 0xFF}, {3, 0xAA}, {4, 0x55}, {5, 0x55},
      {6, 0xAA}, {7, 0xFF}, {8, 0xFF}, {9, 0xFF}, {10, 0xFF}};
  QCOMPARE(trace.size(), expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    QCOMPARE(trace[i].first, expected[i].first);
    QCOMPARE(trace[i].second, expected[i].second);
  }
}

void tst_Gpu::tst_loopDivergence() {
  // Each thread sums 1..tid; trip counts differ per lane.
  const char *kernel = R"(
        S_TID R1
        MOV R2, 0
        MOV R3, 1
    loop:
        SETP.GT P0, R3, R1
        @P0 BRA done
        ADD R2, R2, R3
        ADD R3, R3, 1
        BRA loop
    done:
        S_ARG R4, 0
        SHL R5, R1, 2
        ADD R4, R4, R5
        STG [R4], R2
        EXIT
    )";
  TestMemory mem;
  GpuConfig cfg;
  cfg.numSMs = 1;
  SimtGpu gpu = makeGpu(cfg, mem);
  mem.load(0x100, assembleKernel(kernel));
  LaunchParams lp;
  lp.kernelPc = 0x100;
  lp.blockX = 8;
  lp.gridX = 1;
  lp.args = {{0x800, 0, 0, 0}};
  QVERIFY(gpu.launch(lp));
  runToCompletion(gpu);
  QVERIFY2(!gpu.hasError(), gpu.errorMessage().c_str());
  for (uint32_t i = 0; i < 8; ++i)
    QCOMPARE(mem.read(0x800 + i * 4), i * (i + 1) / 2);
  // Lanes leave the loop one per iteration: 7 divergent branches.
  QCOMPARE(gpu.stats().divergences, uint64_t(7));
  // After the loop all 8 lanes reconverged and executed the STG together.
  QCOMPARE(gpu.stats().blocksCompleted, uint64_t(1));
}

void tst_Gpu::tst_barrier() {
  // Warp 1 writes shared[tid] = tid*10 after a chain of slow loads; warp 0
  // reads shared[15 - tid] after BAR. Without the barrier warp 0 would read
  // zeros.
  const char *kernel = R"(
        S_TID R1
        S_WARP R2
        SETP.EQ P0, R2, 0
        @P0 BRA reader
        S_ARG R6, 0
        LDG R7, [R6]
        LDG R7, [R6+32]
        LDG R7, [R6+64]
        MULI R3, R1, 10
        SHL R4, R1, 2
        STS [R4], R3
        BAR
        EXIT
    reader:
        BAR
        MOV R5, 15
        SUB R5, R5, R1
        SHL R5, R5, 2
        LDS R3, [R5]
        S_ARG R6, 0
        SHL R4, R1, 2
        ADD R6, R6, R4
        STG [R6], R3
        EXIT
    )";
  TestMemory mem;
  GpuConfig cfg;
  cfg.numSMs = 1;
  cfg.memLatency = 20;
  SimtGpu gpu = makeGpu(cfg, mem);
  mem.load(0x100, assembleKernel(kernel));
  LaunchParams lp;
  lp.kernelPc = 0x100;
  lp.blockX = 16; // two warps in one block
  lp.gridX = 1;
  lp.args = {{0x800, 0, 0, 0}};
  QVERIFY(gpu.launch(lp));

  bool sawWaiting = false;
  while (gpu.busy()) {
    gpu.step();
    if (gpu.sms()[0].warps[0].valid && gpu.sms()[0].warps[0].atBarrier)
      sawWaiting = true;
  }
  QVERIFY2(!gpu.hasError(), gpu.errorMessage().c_str());
  QVERIFY(sawWaiting);
  for (uint32_t i = 0; i < 8; ++i)
    QCOMPARE(mem.read(0x800 + i * 4), (15 - i) * 10);
  QCOMPARE(gpu.stats().barriers, uint64_t(2));
}

void tst_Gpu::tst_coalescing() {
  auto runStride = [](unsigned strideWords) {
    const std::string kernel =
        "S_TID R1\nMULI R2, R1, " + std::to_string(strideWords * 4) +
        "\nS_ARG R3, 0\nADD R3, R3, R2\nLDG R4, [R3]\nEXIT\n";
    TestMemory mem;
    GpuConfig cfg;
    cfg.numSMs = 1;
    SimtGpu gpu = makeGpu(cfg, mem);
    mem.load(0x100, assembleKernel(kernel));
    LaunchParams lp;
    lp.kernelPc = 0x100;
    lp.blockX = 8;
    lp.gridX = 1;
    lp.args = {{0x800, 0, 0, 0}};
    gpu.launch(lp);
    runToCompletion(gpu);
    return gpu.stats();
  };
  const auto unit = runStride(1);    // 8 lanes x 4 B = one 32 B segment
  const auto strided = runStride(8); // 32 B apart: one segment per lane
  QCOMPARE(unit.memInstrs, uint64_t(1));
  QCOMPARE(unit.memTransactions, uint64_t(1));
  QCOMPARE(strided.memTransactions, uint64_t(8));
  QVERIFY(strided.cycles > unit.cycles);
}

void tst_Gpu::tst_latencyHiding() {
  auto runWarps = [](uint32_t blockX) {
    const char *kernel =
        "S_ARG R3, 0\nS_TID R1\nSHL R1, R1, 2\nADD R3, R3, R1\n"
        "LDG R4, [R3]\nLDG R5, [R3+64]\nLDG R6, [R3+128]\n"
        "ADD R4, R4, R5\nADD R4, R4, R6\nEXIT\n";
    TestMemory mem;
    GpuConfig cfg;
    cfg.numSMs = 1;
    cfg.warpsPerSM = 8;
    cfg.memLatency = 10;
    SimtGpu gpu = makeGpu(cfg, mem);
    mem.load(0x100, assembleKernel(kernel));
    LaunchParams lp;
    lp.kernelPc = 0x100;
    lp.blockX = blockX;
    lp.gridX = 1;
    gpu.launch(lp);
    runToCompletion(gpu);
    return gpu.stats();
  };
  const auto one = runWarps(8);
  const auto four = runWarps(32);
  const double ipc1 = double(one.issued) / double(one.cycles);
  const double ipc4 = double(four.issued) / double(four.cycles);
  QVERIFY(ipc4 > ipc1);
  QVERIFY(four.idleSlots < 4 * one.idleSlots);
}

void tst_Gpu::tst_launchErrors() {
  TestMemory mem;
  GpuConfig cfg;
  SimtGpu gpu = makeGpu(cfg, mem);
  LaunchParams lp;
  lp.kernelPc = 0x100;
  lp.blockX = 64; // needs 8 warps, an SM only has 4
  QVERIFY(!gpu.launch(lp));
  QVERIFY(gpu.hasError());
  QVERIFY(gpu.done());

  lp.blockX = 8;
  mem.write(0x100, 0xFFFFFFFFu); // undefined opcode
  QVERIFY(gpu.launch(lp));
  QVERIFY(!gpu.hasError());
  runToCompletion(gpu);
  QVERIFY(gpu.hasError());
  QVERIFY(gpu.done());
  QVERIFY(!gpu.busy());

  // reset() clears everything.
  gpu.reset();
  QVERIFY(!gpu.hasError());
  QVERIFY(!gpu.done());
  QCOMPARE(gpu.stats().cycles, uint64_t(0));
}

void tst_Gpu::tst_opcodeTable() {
  // Every enumerator is defined, has a mnemonic and the documented form.
  const std::vector<std::pair<Opcode, Form>> expected = {
      {Opcode::NOP, Form::NONE},  {Opcode::EXIT, Form::NONE},
      {Opcode::BAR, Form::NONE},  {Opcode::BRA, Form::B},
      {Opcode::MOVS, Form::I},    {Opcode::ADD, Form::R},
      {Opcode::FMA, Form::R},     {Opcode::SRA, Form::R},
      {Opcode::ADDI, Form::I},    {Opcode::SRAI, Form::I},
      {Opcode::SETP, Form::R},    {Opcode::SETPI, Form::P},
      {Opcode::LDG, Form::I},     {Opcode::STS, Form::I}};
  for (const auto &e : expected) {
    const unsigned op = static_cast<unsigned>(e.first);
    QVERIFY(opcodeInfo(op).defined());
    QCOMPARE(static_cast<int>(opcodeForm(op)), static_cast<int>(e.second));
    QVERIFY(decode(op << 22).valid);
  }
  QCOMPARE(QString(opcodeName(Opcode::SETPI)), QString("SETP"));
  unsigned defined = 0;
  for (unsigned op = 0; op < 64; ++op)
    if (opcodeInfo(op).defined())
      ++defined;
  QCOMPARE(defined, 29u);
  for (unsigned op : {5u, 6u, 7u, 18u, 23u, 34u, 39u, 44u, 63u}) {
    QVERIFY(!opcodeInfo(op).defined());
    QCOMPARE(static_cast<int>(opcodeForm(op)), static_cast<int>(Form::NONE));
    QVERIFY(!decode(op << 22).valid);
  }
  QVERIFY(!opcodeInfo(1000).defined());
}

void tst_Gpu::tst_divergentBarrier() {
  // Warp 0 diverges and its two paths reach *different* BAR instructions.
  // The barrier must count warp 0 once (when both paths are parked), so it
  // only releases after the slow warp 1 wrote shared memory. With per-path
  // counting the two paths of warp 0 would release it early and warp 0 would
  // read zeros.
  const char *kernel = R"(
        S_TID R1
        S_WARP R2
        SETP.EQ P0, R2, 0
        @P0 BRA reader
        S_ARG R6, 0
        LDG R7, [R6]
        LDG R7, [R6+32]
        LDG R7, [R6+64]
        MULI R3, R1, 10
        SHL R4, R1, 2
        STS [R4], R3
        BAR
        EXIT
    reader:
        ANDI R9, R1, 1
        SETP.NE P1, R9, 0
        @P1 BRA odd
        BAR             # even lanes
        BRA join
    odd:
        BAR             # odd lanes, another barrier site
    join:
        MOV R5, 15
        SUB R5, R5, R1
        SHL R5, R5, 2
        LDS R3, [R5]
        S_ARG R6, 0
        SHL R4, R1, 2
        ADD R6, R6, R4
        STG [R6], R3
        EXIT
    )";
  TestMemory mem;
  GpuConfig cfg;
  cfg.numSMs = 1;
  cfg.memLatency = 20;
  SimtGpu gpu = makeGpu(cfg, mem);
  mem.load(0x100, assembleKernel(kernel));
  LaunchParams lp;
  lp.kernelPc = 0x100;
  lp.blockX = 16;
  lp.gridX = 1;
  lp.args = {{0x800, 0, 0, 0}};
  QVERIFY(gpu.launch(lp));

  bool sawParkedSplit = false;
  while (gpu.busy() && gpu.stats().cycles < 10000) {
    gpu.step();
    const auto &w0 = gpu.sms()[0].warps[0];
    if (w0.valid && w0.paths.size() == 2 && w0.paths[0].parked &&
        w0.paths[1].parked)
      sawParkedSplit = true;
  }
  QVERIFY2(!gpu.hasError(), gpu.errorMessage().c_str());
  QVERIFY(!gpu.busy());
  QVERIFY(sawParkedSplit); // both divergent paths waited at their BAR
  for (uint32_t i = 0; i < 8; ++i)
    QCOMPARE(mem.read(0x800 + i * 4), (15 - i) * 10);
  // One arrival per warp although warp 0 executed BAR on two paths.
  QCOMPARE(gpu.stats().barriers, uint64_t(2));
  QCOMPARE(gpu.stats().divergences, uint64_t(1));
}

void tst_Gpu::tst_barrierWithExitedPath() {
  // Odd lanes exit, even lanes wait at BAR: the warp arrives once the only
  // remaining live lanes are parked (no deadlock).
  const char *kernel = R"(
        S_TID R1
        ANDI R2, R1, 1
        SETP.NE P0, R2, 0
        @P0 BRA leave
        BAR
        S_ARG R4, 0
        SHL R5, R1, 2
        ADD R4, R4, R5
        MOV R6, 7
        STG [R4], R6
    leave:
        EXIT
    )";
  TestMemory mem;
  GpuConfig cfg;
  cfg.numSMs = 1;
  SimtGpu gpu = makeGpu(cfg, mem);
  mem.load(0x100, assembleKernel(kernel));
  LaunchParams lp;
  lp.kernelPc = 0x100;
  lp.blockX = 16; // two warps
  lp.gridX = 1;
  lp.args = {{0x800, 0, 0, 0}};
  QVERIFY(gpu.launch(lp));
  const uint64_t cycles = runToCompletion(gpu, 5000);
  QVERIFY(cycles < 5000);
  QVERIFY2(!gpu.hasError(), gpu.errorMessage().c_str());
  for (uint32_t i = 0; i < 16; ++i)
    QCOMPARE(mem.read(0x800 + i * 4), (i & 1) ? 0u : 7u);
  QCOMPARE(gpu.stats().barriers, uint64_t(2));
}

void tst_Gpu::tst_launchWhileBusy() {
  TestMemory mem;
  GpuConfig cfg;
  SimtGpu gpu = makeGpu(cfg, mem);
  const uint32_t kpc = 0x1000, A = 0x2000, B = 0x3000, C = 0x4000, N = 21;
  mem.load(kpc, assembleKernel(kVecAdd));
  for (uint32_t i = 0; i < N; ++i) {
    mem.write(A + i * 4, i);
    mem.write(B + i * 4, 100 * i);
  }
  LaunchParams lp;
  lp.kernelPc = kpc;
  lp.blockX = 16;
  lp.gridX = 2;
  lp.args = {{A, B, C, N}};
  QVERIFY(gpu.launch(lp));
  gpu.step();
  gpu.step();

  // A second doorbell while BUSY - even with invalid parameters - is ignored
  // and must not poison the running kernel.
  LaunchParams bad = lp;
  bad.blockX = 4096;
  QVERIFY(!gpu.launch(bad));
  QVERIFY(!gpu.hasError());
  QVERIFY(gpu.busy());
  QCOMPARE(gpu.launchParams().blockX, 16u);

  runToCompletion(gpu);
  QVERIFY(gpu.done());
  QVERIFY2(!gpu.hasError(), gpu.errorMessage().c_str());
  for (uint32_t i = 0; i < N; ++i)
    QCOMPARE(mem.read(C + i * 4), 101 * i);
}

void tst_Gpu::tst_busFault() {
  // A memory callback that refuses an address (as IOGpu does for its own
  // register window) stops the kernel with an error instead of hanging.
  TestMemory mem;
  GpuConfig cfg;
  cfg.numSMs = 1;
  SimtGpu *gpuPtr = nullptr;
  SimtGpu gpu(
      cfg,
      [&](uint32_t a) -> uint32_t {
        if (a >= 0xF000 && a < 0xF040) {
          gpuPtr->fault("read from protected window");
          return 0;
        }
        return mem.read(a);
      },
      [&](uint32_t a, uint32_t v) { mem.write(a, v); });
  gpuPtr = &gpu;
  mem.load(0x100, assembleKernel("S_ARG R1, 0\nLDG R2, [R1+4]\nMOV R3, 1\n"
                                 "STG [R1+64], R3\nEXIT\n"));
  LaunchParams lp;
  lp.kernelPc = 0x100;
  lp.blockX = 8;
  lp.gridX = 1;
  lp.args = {{0xF000, 0, 0, 0}};
  QVERIFY(gpu.launch(lp));
  const uint64_t cycles = runToCompletion(gpu, 1000);
  QVERIFY(cycles < 1000);
  QVERIFY(gpu.done());
  QVERIFY(gpu.hasError());
  QVERIFY2(gpu.errorMessage().find("protected window") != std::string::npos,
           gpu.errorMessage().c_str());
  QVERIFY(gpu.errorMessage().find("kernel pc 1") != std::string::npos);
  QCOMPARE(mem.read(0xF040), 0u); // the following STG never ran

  // The fault does not leak into the next launch.
  lp.args = {{0x800, 0, 0, 0}};
  QVERIFY(gpu.launch(lp));
  runToCompletion(gpu);
  QVERIFY2(!gpu.hasError(), gpu.errorMessage().c_str());
  QCOMPARE(mem.read(0x840), 1u);
}

void tst_Gpu::tst_coalescingStats() {
  auto run = [](unsigned strideWords) {
    const std::string kernel =
        "S_TID R1\nMULI R2, R1, " + std::to_string(strideWords * 4) +
        "\nS_ARG R3, 0\nADD R3, R3, R2\nLDG R4, [R3]\nSTG [R3], R4\nEXIT\n";
    TestMemory mem;
    GpuConfig cfg;
    cfg.numSMs = 1;
    SimtGpu gpu = makeGpu(cfg, mem);
    mem.load(0x100, assembleKernel(kernel));
    LaunchParams lp;
    lp.kernelPc = 0x100;
    lp.blockX = 8;
    lp.gridX = 1;
    lp.args = {{0x800, 0, 0, 0}};
    gpu.launch(lp);
    runToCompletion(gpu);
    return gpu.stats();
  };
  const auto unit = run(1);
  QCOMPARE(unit.coalescedInstrs, uint64_t(2));
  QCOMPARE(unit.coalescedTxns, uint64_t(2));
  QCOMPARE(unit.uncoalescedInstrs, uint64_t(0));
  const auto strided = run(8);
  QCOMPARE(strided.coalescedInstrs, uint64_t(0));
  QCOMPARE(strided.uncoalescedInstrs, uint64_t(2));
  QCOMPARE(strided.uncoalescedTxns, uint64_t(16));
  QCOMPARE(strided.coalescedTxns + strided.uncoalescedTxns,
           strided.memTransactions);
}

void tst_Gpu::tst_stateSnapshot() {
  // saveState()/restoreState() reproduce the exact execution (the basis of
  // reverse execution in IOGpu). Also reports the snapshot size used to size
  // the reverse history.
  TestMemory mem;
  GpuConfig cfg;
  SimtGpu gpu = makeGpu(cfg, mem);
  const uint32_t kpc = 0x1000, A = 0x2000, B = 0x3000, C = 0x4000, N = 21;
  mem.load(kpc, assembleKernel(kVecAdd));
  for (uint32_t i = 0; i < N; ++i) {
    mem.write(A + i * 4, i);
    mem.write(B + i * 4, 100 * i);
  }
  LaunchParams lp;
  lp.kernelPc = kpc;
  lp.blockX = 16;
  lp.gridX = 2;
  lp.args = {{A, B, C, N}};
  QVERIFY(gpu.launch(lp));
  for (int i = 0; i < 20; ++i)
    gpu.step();
  const SimtGpu::State mid = gpu.saveState();
  const size_t midBytes = SimtGpu::stateBytes(mid);
  runToCompletion(gpu);
  const GpuStats first = gpu.stats();
  QVERIFY(gpu.done());

  gpu.restoreState(mid);
  QVERIFY(gpu.busy());
  QCOMPARE(gpu.stats().cycles, uint64_t(20));
  runToCompletion(gpu);
  const GpuStats second = gpu.stats();
  QCOMPARE(second.cycles, first.cycles);
  QCOMPARE(second.issued, first.issued);
  QCOMPARE(second.threadInstrs, first.threadInstrs);
  QCOMPARE(second.memTransactions, first.memTransactions);
  for (uint32_t i = 0; i < N; ++i)
    QCOMPARE(mem.read(C + i * 4), 101 * i);

  // Snapshot size measurement: default configuration while running and the
  // largest configuration (8 SM x 16 warps x 32 lanes, fully occupied).
  GpuConfig big;
  big.numSMs = 8;
  big.warpsPerSM = 16;
  big.warpSize = 32;
  SimtGpu large = makeGpu(big, mem);
  LaunchParams blp = lp;
  blp.blockX = 32;
  blp.gridX = 8 * 16;
  QVERIFY(large.launch(blp));
  for (int i = 0; i < 40; ++i)
    large.step();
  const size_t bigBytes = SimtGpu::stateBytes(large.saveState());
  qInfo() << "GPU snapshot size: default config (2x4x8, 2 blocks resident):"
          << midBytes << "bytes; max config (8x16x32, fully occupied):"
          << bigBytes << "bytes";
  QVERIFY(midBytes > 0 && midBytes < 64 * 1024);
  QVERIFY(bigBytes < 1024 * 1024);
}

QTEST_APPLESS_MAIN(tst_Gpu)
#include "tst_gpu.moc"

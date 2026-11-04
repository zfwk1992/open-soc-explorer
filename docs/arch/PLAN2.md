# 第二阶段计划：用户文档、GUI 清晰化、中英双语（PLAN2）

> 日期：2026-10-29。第一阶段（GPU/NPU/NeoGeo/PS3/架构浏览器）已完成并通过全部测试，见 [PLAN.md](PLAN.md)。
> 本阶段目标：**(1) 大量的使用文档（教用户怎么用）；(2) 整个 GUI 更清楚；(3) 文档与 GUI 都做成中文/英文双语。**
> 同时修掉第一阶段代码评审发现的缺陷（见 §6）。

## 1. 总体约定（所有 agent 必读）

### 1.1 语言机制（已搭好，直接用）

- 设置项 `RIPES_SETTING_LANGUAGE`（`src/ripessettings.h`）：`"system" | "en" | "zh_CN"`，重启生效。
- `src/i18n.h`：`Ripes::uiLanguage()`、`uiIsChinese()`、`localized(QJsonValue)`、`bilingual(en, zh)`、`installTranslators(app)`。
  `main.cpp` 已在 `QApplication` 创建后调用 `installTranslators`。
- **界面字符串一律用 `tr("...")`**（`Q_OBJECT` 类内）或 `QObject::tr` / `QCoreApplication::translate("Context", "...")`；`.ui` 文件里的字符串默认可翻译。
  源字符串写英文；中文通过 `.ts` 翻译文件提供。只有在无法用 `tr()` 的地方（例如非 QObject 的纯数据）才用 `Ripes::bilingual(en, zh)`。
- 翻译文件：`resources/i18n/<module>_zh_CN.ts`，模块 → 源文件映射在 `tools/i18n/modules.json`。
  生成/更新：`python tools/i18n/update_ts.py <module>`（用 `P:\workspace\soc\toolchain\venv\Scripts\python.exe`，脚本会自动找到 lupdate）。
  然后直接编辑 `.ts`（XML）：填 `<translation>` 并去掉 `type="unfinished"`。CMake 会用 lrelease 编译并把 `.qm` 嵌入 `:/i18n/`。
  **每个 agent 只拥有自己模块的 `.ts`**：`core`、`tabs`、`archexplorer`、`iogpu`、`ionpu`。
- JSON 内容（拓扑文件）的双语：任何面向用户的字符串字段都允许写成 `{"zh": "...", "en": "..."}`，加载时用 `Ripes::localized()` 解析（纯字符串仍然合法）。
- 帮助入口：`src/help.h` 的 `Ripes::showHelp("<章节文件名>")`，例如 `showHelp("09-gpu")`。各模块的 "?" 按钮调用它；实现由 core agent 替换为应用内帮助窗口，其他 agent 不改 `help.cpp`。

### 1.2 GUI "更清楚" 的统一标准

每个标签页 / 外设部件都要满足：

1. 顶部有**标题 + 一句话说明**（这是什么、看什么），右侧有 "?" 帮助按钮（`Ripes::showHelp`）。
2. 每个控件都有 tooltip（鼠标悬停）和 statusTip（状态栏），说清楚"做什么、单位是什么"。
3. 数值显示带单位和含义（例如 `Cycles: 48`、`DMA bytes: 128`），寄存器用"名字 + 偏移 + 当前值 + 含义"表格，不是裸数字。
4. 状态用颜色 + 文字双重表达（不只靠颜色）；颜色从调色板取（`QPalette`），深浅色主题都可读（`Ripes::ThemeManager` 可监听主题变化）。
5. 图例（legend）：有颜色编码的视图必须有图例。
6. 空状态要有提示文字（例如"尚未启动 kernel，请写 CTRL.START"）。
7. 布局用 `QSplitter`/滚动区，窗口缩小不截断；关键信息在 1280×800 下可见。
8. 所有新增字符串 `tr()`，并补全该模块 `.ts` 的中文翻译。

### 1.3 文档结构（docs/guide）

```
docs/guide/README.md            双语索引（两种语言的目录都在这一个文件里）
docs/guide/zh/NN-xxx.md         中文章节
docs/guide/en/NN-xxx.md         英文章节（文件名与中文一一对应，内容一一对应）
docs/guide/images/              截图（命名：NN-xxx-<name>.zh.png / .en.png；无语言差异的用 .png）
docs/guide/images/manifest_<agent>.json   截图需求清单（第二轮由截图 agent 批量生成）
```

章节（固定文件名，禁止自行增删编号）：

| 文件 | 内容 | 负责 |
|---|---|---|
| `README.md` | 双语索引、阅读路线（学生/教师/开发者）、各章一句话简介 | docs-start |
| `01-install.md` | 安装与构建：Windows 用户态工具链 + `build.ps1`、Linux/macOS 通用步骤、常见构建错误 | docs-start |
| `02-quickstart.md` | 10 分钟上手：打开示例、汇编、单步、看寄存器/内存、运行到结束 | docs-start |
| `03-gui-tour.md` | GUI 总览：主窗口、每个标签页（Editor/Processor/Cache/Memory/I/O/Architecture）、工具栏、设置、语言切换、快捷键 | docs-start |
| `04-riscv-assembly.md` | RISC-V 汇编教程：寄存器、指令类、伪指令、数据段、分支/循环、函数调用、ecall、练习 | docs-riscv |
| `05-c-programs.md` | 用 C 编程：配置编译器、示例、链接参数、常见问题 | docs-riscv |
| `06-processors-pipelines.md` | 处理器模型：单周期/5 级/6 级、冒险与转发、流水线图、反向执行、VCD | docs-riscv |
| `07-cache.md` | 缓存模拟：参数、命中/缺失、替换策略、预设、图表 | docs-riscv |
| `08-mmio-peripherals.md` | MMIO 外设：地址映射、LED 矩阵/开关/方向键/RTC/屏幕、符号用法 | docs-riscv |
| `09-gpu.md` | SIMT GPU 教程：寄存器表、kernel ISA、`gpu_asm.py`、vecadd 逐步、分支发散/合并/bank 冲突实验、练习与答案 | docs-accel |
| `10-npu.md` | NPU 教程：描述符环、门铃、轮询 vs 中断、DMA、matmul 逐步、CPU 与 NPU 周期对比、练习与答案 | docs-accel |
| `11-arch-explorer.md` | 架构浏览器用法：加载拓扑、数据流步进/播放、缩放、方块详情、内置四个拓扑 | docs-arch |
| `12-neogeo.md` | NeoGeo 导览：按数据流讲解 + `neogeo_mailbox.s` 实验 | docs-arch |
| `13-ps3-cell.md` | PS3/Cell 导览：按数据流讲解 + `spe_double_buffer.c` 实验 | docs-arch |
| `14-authoring-topologies.md` | 编写自己的拓扑 JSON：schema、双语字段、校验器、渲染工具 | docs-arch |
| `15-cli.md` | 命令行模式：参数、遥测输出、批量跑程序、限制（CLI 不能实例化外设） | docs-start |
| `16-faq.md` | FAQ 与故障排除（构建、运行、汇编报错、外设、性能） | docs-start |
| `17-glossary.md` | 术语表（中英对照：SIMT、warp、coalescing、systolic、doorbell、EIB、MFC…） | docs-accel |

文档写作规范：

- 面向**第一次使用的学生**：每章开头"你将学到 / 前置条件"，正文按"做一步 → 看到什么 → 为什么"组织，配截图占位（见下），结尾"练习"+"小结"。
- 中文版用中文叙述、保留英文术语（首次出现给出英文）；英文版是完整对应版本，不是摘要。两个版本段落一一对应。
- 所有菜单/按钮名字用 GUI 中的英文原文加中文（例如 "**Processor** 标签页（处理器）"），因为用户可能在任一语言下使用。
- 涉及的代码/命令放代码块；路径用仓库相对路径。
- 文档中引用示例程序时，给出 Examples 菜单中的名字和文件路径。
- **准确性来源**：读代码（`src/*.cpp/.ui`、`src/io/*`、`src/archexplorer/*`、`docs/arch/*.md`、`examples/*`），不要凭记忆写 Ripes 功能。拿不准的写"（待核实）"并在最终报告里列出。
- 截图：在正文用 `![说明](../images/NN-xxx-name.zh.png)` 引用，并在 `docs/guide/images/manifest_<agent>.json` 登记：
  ```json
  [{"file": "09-gpu-widget.zh.png", "lang": "zh", "tab": "io", "peripheral": "SIMT GPU",
    "example": "examples/assembly/gpu_vecadd.s", "actions": ["assemble", "run"], "caption": "..."}]
  ```
  第二轮由截图 agent 用离屏渲染统一生成（zh/en 各一张）。

### 1.4 工作目录与构建规则（避免互相踩）

- **代码 agent 各自在独立副本里工作**：`P:\workspace\soc\agents\<agent>\`（已从主树复制，不含 build/）。
  构建：`powershell -ExecutionPolicy Bypass -File P:\workspace\soc\build.ps1 -Repo P:\workspace\soc\agents\<agent> -Jobs 2 -Target <目标>`
  （首次会全量编译，约 10–20 分钟；之后增量。`-Tests <名字>` 跑指定测试；无头运行加 `QT_QPA_PLATFORM=offscreen`。）
  最终我按**文件所有权**把每个副本里改动的文件合并回主树，所以：**只改自己拥有的文件**，需要改共享文件（`src/CMakeLists.txt`、`test/CMakeLists.txt`、`mainwindow.*`、`examples.qrc`、`docs/README.md`、顶层 `README.md`）时，在最终报告里写出精确的改动，由集成者合并。
- **文档 agent 直接在主树** `P:\workspace\soc\open-soc-explorer` 下写 `docs/guide/`（互不重叠的章节）。可以运行主树里的 `build\Ripes.exe`（只读，不要重新构建主树）。
- 不提交 git、不改 TLS/证书设置、不安装系统级软件。
- 最终报告必须包含：改动/新增文件完整清单；构建与测试结果（原文数字）；未完成项与原因；需要集成者做的共享文件改动。

## 2. 代码 agent 分工

### gpu（副本 `agents\gpu`）
拥有：`src/accel/gpu/*`、`src/io/iogpu*`、`test/tst_gpu.cpp`、`test/tst_accel_e2e.cpp`、`tools/gpu_asm.py`、`examples/assembly/gpu_vecadd.s`、`resources/i18n/iogpu_zh_CN.ts`、`docs/arch/gpu.md`。
任务：
1. 修评审缺陷 §6 (1)(3)(4)(6)(7)(8)(9-GPU 部分)。
2. 重做 GPU 外设部件使其"更清楚"（§1.2）：建议拆为 `src/io/iogpuwidget.{h,cpp}`：状态条（IDLE/BUSY/DONE/ERROR + 周期数 + 错误原因）、寄存器表、SM/warp/lane 可视化（活跃掩码、PC、分支发散高亮、图例）、统计面板（指令数、合并访存次数、bank 冲突数）、kernel 反汇编视图（当前 PC 高亮）、"?" 帮助 → `showHelp("09-gpu")`。
3. 字符串 `tr()` + `iogpu_zh_CN.ts` 翻译完整。
4. 测试全绿：`tst_gpu`、`tst_accel_e2e`；GUI 离屏启动不崩。

### npu（副本 `agents\npu`）
拥有：`src/accel/npu/*`、`src/io/iotensorunit*`、`test/tst_npu.cpp`、`examples/C/npu_matmul.c`、`examples/assembly/npu_matmul.s`、`resources/i18n/ionpu_zh_CN.ts`、`docs/arch/tensor_cpu.md`。
任务：
1. 修评审缺陷 §6 (2)(9-NPU 部分)。
2. 重做 NPU 部件（§1.2）：状态机图（IDLE→FETCH_DESC→DMA_IN→COMPUTE→DMA_OUT→COMPLETE 当前态高亮）、描述符环视图（HEAD/TAIL 指针、每个描述符字段解码）、脉动阵列动画/热图（当前 MAC 活跃格）、DMA 统计、中断状态（IRQ_EN/IRQ_STATUS 位含义）、寄存器表、"?" → `showHelp("10-npu")`。
3. `tr()` + `ionpu_zh_CN.ts`。
4. 测试全绿：`tst_npu`（新的 e2e 测试写进 `tst_npu.cpp`，不新增测试可执行文件）。

### arch（副本 `agents\arch`）
拥有：`src/archexplorer/*`、`resources/arch/*`、`tools/validate_arch_json.py`、`tools/arch_render/*`、`test/tst_archmodel.cpp`、`docs/arch/arch_explorer.md`、`resources/i18n/archexplorer_zh_CN.ts`、`docs/arch/images/*`。
任务：
1. 拓扑 JSON 双语：所有 `title/description/label/sublabel/flows[].name/steps[].text` 等改成 `{"zh","en"}`（四个内置拓扑 + minimal 示例），加载用 `Ripes::localized()`；校验器支持该形式。
2. 架构浏览器 GUI 更清楚（§1.2）：顶部拓扑选择 + 说明；图例（方块种类/总线种类颜色）；数据流面板：步骤列表（当前步高亮、可点击跳转）、每步说明文字区、播放/暂停/速度；方块详情面板（点击方块显示字段）；缩放到适合、重置、导出 PNG；键盘快捷键（←/→ 步进、空格播放）。
3. `tr()` + `archexplorer_zh_CN.ts`。
4. `tst_archmodel` 全绿；`arch_render` 工具重新渲染 `docs/arch/images/*.png`（中文版）。

### core（副本 `agents\core`） — 主窗口 / 设置 / 编辑器 / 对话框的清晰化与中文化
拥有：`tools/i18n/modules.json` 里 `core` 模块的全部文件、`src/help.cpp`（可新增 `src/helpdialog.*`）、`main.cpp`、`resources/i18n/core_zh_CN.ts`、`docs/guide/guide.qrc`（若做应用内帮助）。
任务：
1. 设置对话框 Environment 页加 "Language / 语言" 下拉（System / English / 简体中文），写 `RIPES_SETTING_LANGUAGE`，改后提示重启生效。
2. 应用内帮助：`Ripes::showHelp(chapter)` 打开一个 Help 窗口（`QTextBrowser::setMarkdown`），从 `:/guide/<lang>/<chapter>.md` 读取（`docs/guide/guide.qrc` 用 CMake `file(GLOB)` 自动收集 `docs/guide/**/*.md` 与 `images/*`；章节不存在时回退到 README）。主菜单加 **Help → User Guide / 用户手册**、**Help → About**。
3. 主窗口清晰化：标签页名称和图标带 tooltip；工具栏动作带 statusTip；首次启动显示"欢迎/快速开始"面板（可关闭、设置里可重新打开）；状态栏显示当前处理器/ISA/语言。
4. `core` 模块所有用户可见字符串改 `tr()`（含菜单、动作、对话框、编辑器提示、处理器选择对话框里的处理器描述），`core_zh_CN.ts` 翻译 100%。
5. 构建 Ripes.exe 成功；手动验证：设置语言为中文重启后主窗口为中文。

### tabs（副本 `agents\tabs`） — Memory/Cache/I/O/流水线/寄存器部件的清晰化与中文化
拥有：`modules.json` 里 `tabs` 模块的全部文件、`resources/i18n/tabs_zh_CN.ts`。
任务：
1. 这些部件的 "更清楚"（§1.2）：每个标签页顶部说明 + "?"（`showHelp("07-cache")` 等）；缓存标签页的参数带解释 tooltip；内存标签页列/进制说明；I/O 标签页：外设列表带描述、"添加外设"流程更明显、寄存器映射表带含义列；流水线图图例。
2. 所有字符串 `tr()`，`tabs_zh_CN.ts` 翻译 100%（包括内置外设的 `description()`/寄存器描述）。
3. 不改 `mainwindow.*`；需要的话在报告中写明。构建 Ripes.exe 成功、`tst_*` 不回归。

## 3. 文档 agent 分工（直接在主树写）

| agent | 章节 | 额外产出 |
|---|---|---|
| docs-start | README、01、02、03、15、16 | `images/manifest_docs-start.json` |
| docs-riscv | 04、05、06、07、08 | `images/manifest_docs-riscv.json` |
| docs-accel | 09、10、17 | `images/manifest_docs-accel.json`；可在 `examples/` 下新增练习用程序（命名 `exercise_*`，并在报告里列出需加入 `examples.qrc` 的条目） |
| docs-arch | 11、12、13、14 | `images/manifest_docs-arch.json` |

每章的 zh 与 en 都由同一 agent 写，保证对应。GUI 正在被改（§2），文档按 §2 描述的**目标 GUI** 写，拿不准的控件名写通用叫法并标"（待核实）"；第二轮截图时统一校对。

## 4. 第二轮（代码合并后）

1. 集成者合并五个副本 → 主树，全量构建 + 全部测试。
2. 截图 agent：`tools/gui_shots/`（离屏 QApplication，加载示例、切标签页、`grab()` 到 PNG），按四个 manifest 生成 zh/en 截图；校对文档中的控件名与实际 GUI。
3. 翻译补漏：`python tools/i18n/update_ts.py --all --stats` 必须全部 0 untranslated。
4. 顶层 `README.md`、`docs/README.md` 链接到 `docs/guide/README.md`。

## 5. 验收标准

- `build.ps1` 全量通过，所有 `tst_*` 通过，Ripes.exe 在 `QT_QPA_PLATFORM=offscreen` 下启动 20 s 不崩（en 与 zh_CN 各一次）。
- 17 章 × 2 语言全部存在且非空，截图齐全；`docs/guide/README.md` 索引无死链。
- 五个 `.ts` 0 untranslated。
- 每个标签页/外设部件满足 §1.2 的 8 条。

## 6. 第一阶段代码评审缺陷清单（必须修）

1. **`src/io/iogpu.cpp` 死锁**：`processorWasClocked()` 持 `m_mutex` 调 `m_gpu.step()`，kernel 的 LDG/STG 若落到 GPU 自身 MMIO 区会重入 `ioRead/ioWrite` → 非递归 `QMutex` 死锁。修法：不要在持锁时回调内存；把 step 产生的内存访问在锁外执行，或改成 `QRecursiveMutex`，或在回调里检测自映射并返回错误。
2. **`src/io/iotensorunit.cpp` 同样的死锁**（`QUEUE_BASE`/描述符地址指向 NPU 自身寄存器区）。
3. **`iogpu.cpp` 运行中再写 CTRL.START**：`launch()` 失败却置 `DONE|ERROR`，且 `m_error` 不清，导致原 kernel 正常完成也被标 ERROR。修法：BUSY 时忽略并只置一个"launch ignored"状态位/日志，不污染正在跑的 kernel。
4. **`simtgpu.cpp` BAR 屏障**：分叉 warp 的每条路径到达 BAR 都 `warpsAtBarrier++`，屏障可能提前释放。修法：按 warp 计数（一个 warp 只能算一次），路径合并后再参与屏障，或要求 BAR 在收敛后执行（否则报错）。
5. **`README.md` 的 `\b` 字节** —— 已由集成者修复。
6. **`iogpu.cpp` 每周期 `std::map` 查找 + `QVariant::toUInt()`**，且与 GUI 线程的 `setParameter` 有数据竞争。修法：`applyParameters()` 时缓存成 `std::atomic<unsigned>`。
7. **`test/tst_accel_e2e.cpp` 手抄 kernel 机器码**：改为用 `gpu::assembleKernel()` 从源文本生成。
8. **`simtgpu.cpp` `opcodeDefined()` 与 `opcodeForm()` 重复枚举**：合并成一张表。
9. **反向执行**：GPU/NPU 外设只响应 `processorClocked`，不响应 `ProcessorHandler::processorReversed`。修法（两者选其一并写进文档）：(a) 忙时每周期把模型状态快照进一个有界队列（上限 `RIPES_SETTING_REWINDSTACKSIZE`），`processorReversed` 时弹出一帧；(b) 若状态过大，至少在 reverse 时把外设复位到"启动前"并在部件上明示"反向执行后加速器已复位"。

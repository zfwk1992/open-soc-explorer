# SNK Neo Geo（MVS / AES）架构

> 模块 C：双 CPU 街机架构。配套的拓扑描述在 `resources/arch/neogeo.json`（在 Architecture tab 中浏览），
> 教学示例在 `examples/assembly/neogeo_mailbox.s`。对照阅读：[GPU 运行原理](gpu.md)、[PS3 / Cell](ps3_cell.md)。
>
> 数据来源以 [NeoGeo Development Wiki](https://wiki.neogeodev.org) 为准；文中 `$` 前缀表示十六进制。

![Neo Geo 系统框图（Architecture tab 渲染，`resources/arch/neogeo.json`）](images/neogeo.png)

---

## 1. 系统概览

Neo Geo 是 SNK 于 1990 年推出的街机（MVS，Multi Video System）与家用机（AES，Advanced Entertainment System）
共用的硬件平台：**同一块卡带可以在两者上运行**，两者的差别只在机柜 I/O、DIP 开关、backup RAM 与板载 fix 图形复用器。

它是一台典型的"80 年代末街机架构"：

| 组件 | 型号 / 规格 | 作用 |
|---|---|---|
| 主 CPU | Motorola 68000，12 MHz，16-bit 数据 / 24-bit 地址 | 游戏逻辑、填写 VRAM 中的 sprite 控制块、发送音效命令 |
| 音频 CPU | Zilog Z80，4 MHz，8-bit，64 KiB 地址空间 | 独立运行音效驱动，驱动 YM2610 |
| 音源 | Yamaha YM2610（OPNB），8 MHz | FM 4 ch + SSG 3 ch + ADPCM-A 6 ch + ADPCM-B 1 ch |
| 视频 | LSPC（LSPC-A0 / LSPC2-A2，"Line SPrite Controller"）+ NEO-B1 | 逐扫描线合成 sprite 与 fix layer，无帧缓冲 |
| VRAM | 64 KiB + 4 KiB（32K + 2K 个 16-bit word） | 只存 sprite 属性、fix 地图、sprite 列表，**不存像素** |
| Palette RAM | 2 bank × 256 palette × 16 色 × 16-bit（每 bank 8 KiB） | 颜色查找表 |
| Work RAM | 64 KiB（68000） | 游戏工作内存 |
| Z80 RAM | 2 KiB | 音效驱动工作内存 |
| 分辨率 | 320×224 可见（总 384×264，NTSC） | 像素时钟 6 MHz，约 59.19 Hz |
| 卡带 | 两块 PCB：PROG（P/V ROM）+ CHA（C/S/M1 ROM） | 图形、音频、程序三条总线分别直连 ROM |

最重要的架构特征（后面会反复出现）：

1. **三条互不相通的总线**：68000 总线、Z80 音频总线、LSPC 图形总线。68000 **不能**读取 C ROM（sprite 图形）、
   S ROM（fix 图形）、V ROM（采样）里的数据——它只能通过 VRAM 中的"索引"告诉 LSPC 去哪里取。
2. **没有帧缓冲（framebuffer）**：LSPC 在每条扫描线上实时从 ROM 取 tile、写入行缓冲（line buffer）、输出到 DAC。
3. **没有 DMA**：所有 VRAM / palette RAM 写入都由 68000 通过 MMIO 寄存器逐 word 完成。
4. **双 CPU 松耦合**：68000 与 Z80 之间只有一个 8-bit 的 sound latch（mailbox）和一根 NMI 线。

### 1.1 整体框图

```
                 68000 bus (16-bit data / 24-bit addr)
  ┌────────────┬────────────┬────────────┬────────────┬──────────────┐
  │            │            │            │            │              │
┌─┴────────┐ ┌─┴───────┐ ┌──┴──────┐ ┌───┴──────┐ ┌───┴──────┐ ┌─────┴──────┐
│ MC68000  │ │Work RAM │ │ BIOS    │ │ Backup   │ │ P ROM    │ │ Palette RAM│
│ 12 MHz   │ │ 64 KiB  │ │ 128 KiB │ │ RAM 64K  │ │ (cart,   │ │ 2×8 KiB    │
│          │ │$100000  │ │$C00000  │ │ $D00000  │ │ PROG bd) │ │ $400000    │
└─┬────────┘ └─────────┘ └─────────┘ └──────────┘ └──────────┘ └──────┬─────┘
  │  REG_SOUND $320000 (8-bit latch, NEO-C1)                          │ PA0~PA11
  │  ┌───────────────────── sound latch ────────────────────┐          │
  │  │  cmd  68k → Z80  (写入触发 Z80 NMI)                  │          │
  │  │  reply Z80 → 68k (port $0C → REG_SOUND 读)            │          │
  │  └───────────────────────────────────────────────────────┘          │
  │  REG_VRAMADDR/RW/MOD $3C0000..                                      │
┌─┴──────────┐  VRAM bus   ┌──────────┐   P bus (24-bit, C/S ROM addr)  │
│ LSPC2-A2   │◄───────────►│ VRAM     │   ┌──────────────┐              │
│ 行精灵控制器│            │64K+4K    │   │ CHA board    │              │
│ 6 MHz pix  │────────────────────────────►│ C ROM (tiles)│──► NEO-ZMC2 ─┐
│ IRQ1 VBL   │                             │ S ROM (fix)  │──► FIX0~7   │
│ IRQ2 timer │                             │ M1 ROM (Z80) │             │
└─┬──────────┘                             └──────┬───────┘             │
  │ CK/WE/SS                                      │ Z80 addr (bank)     ▼
┌─┴──────────┐  line buffers ×4                                   ┌──────────┐
│ NEO-B1     │◄────────────────────── serialized pixels ──────────│          │
│ 行缓冲+调色 │────── palette addr ──► Palette RAM ──► latch ──►  │ Video DAC│──► RGB / JAMMA
│ +watchdog  │                                                    └──────────┘
└────────────┘
                     ───────── Z80 sound bus (8-bit) ─────────
  ┌──────────┐  ┌──────────┐  ┌───────────┐  ┌───────────────┐  ┌─────────┐
  │ Z80 4 MHz│──│ Z80 RAM  │──│ M1 ROM    │──│ YM2610 8 MHz  │──│ YM3016  │──► 音频输出
  │ NMI/IRQ  │  │ 2 KiB    │  │ (CHA bd)  │  │ FM/SSG/ADPCM  │  │ DAC     │
  └──────────┘  └──────────┘  └───────────┘  └──────┬────────┘  └─────────┘
                                                     │ ADPCM bus (RA / RB)
                                               ┌─────┴─────┐
                                               │ V ROM     │ (PROG board)
                                               │ ADPCM-A/B │
                                               └───────────┘
```

### 1.2 时钟树

主时钟（mclk）为 24 MHz（AES 为 24.167829 MHz），由 NEO-D0 和 LSPC 分频：

| 时钟 | 频率 | 分频 | 用途 |
|---|---|---|---|
| mclk | 24 MHz | — | 基准 |
| 68KCLK | 12 MHz | ÷2 | 68000 |
| 12M | 12 MHz | ÷2 | NEO-ZMC2 串行化器（C ROM 像素输出） |
| 8M | 8 MHz | ÷3（LSPC 输出） | YM2610 |
| 4M | 4 MHz | ÷6（LSPC 输出） | Z80 |
| pixel clock | 6 MHz | ÷4 | 每像素 4 mclk；LSPC 定时器也用这个时钟递减 |

一条扫描线 = 384 像素 = 1536 mclk = 64 µs → 行频 15.625 kHz；一帧 = 264 行 → 59.19 Hz（NTSC）。
PAL 为 312 行 → 约 50.08 Hz，并且上下各多出 16 行可见区域（256 行可见）。

---

## 2. 68000 主 CPU

### 2.1 内存映射（卡带系统）

| 起始 | 结束 | 大小 | 名称 | 说明 |
|---|---|---|---|---|
| $000000 | $0FFFFF | 1 MiB | P ROM bank 1 | 向量表 + 程序固定段（卡带 PROG board）。ROMWAIT 决定 0/1 等待周期 |
| $100000 | $10F2FF | ~61 KiB | WORKRAM | 用户工作 RAM |
| $10F300 | $10FFFF | ~3 KiB | — | 系统 ROM（BIOS）保留的 RAM：BIOS 变量、调用参数 |
| $110000 | $1FFFFF | — | — | Work RAM 镜像 |
| $200000 | $2FFFFF | 1 MiB | P ROM bank 2 / PORT | 第二 P ROM 窗口；>2 MiB 的游戏在此范围写入进行 bankswitch（PROG board 上的锁存器，D0/D1 → P2 ROM A20/A21） |
| $300000 | $3FFFFF | — | I/O | 输入、输出、系统寄存器、LSPC 寄存器（见下表） |
| $400000 | $401FFF | 8 KiB | PALETTES | 当前 bank 的 palette RAM（$402000–$7FFFFF 镜像） |
| $800000 | $BFFFFF | ≤8 MiB | MEMCARD | 记忆卡（8-bit，2 等待周期） |
| $C00000 | $C1FFFF | 128 KiB | SYSTEMROM | BIOS（$C20000–$CFFFFF 镜像） |
| $D00000 | $D0FFFF | 64 KiB | — | Backup RAM（仅 MVS；$D10000–$DFFFFF 镜像） |

注意 **VRAM 不在 68000 地址空间内**：只能通过 $3C0000 的三个 LSPC 寄存器间接访问。

### 2.2 I/O 寄存器（$300000–$38xxxx）

| 地址 | 名称 | 读 | 写 |
|---|---|---|---|
| $300000 | REG_P1CNT | P1 摇杆/按键（低有效） | — |
| $300001 | REG_DIPSW | 硬件 DIP 开关 | **踢 watchdog**（任意值） |
| $300081 | REG_SYSTYPE | Test 按钮、槽位类型、系统 ID | — |
| $320000 | REG_SOUND | **Z80 的应答字节** | **发送命令给 Z80（触发 NMI）** |
| $320001 | REG_STATUS_A | RTC 数据/脉冲、投币 1–4、Service | — |
| $340000 | REG_P2CNT | P2 摇杆/按键 | — |
| $380000 | REG_STATUS_B | AES/MVS 标志、记忆卡状态、Start/Select | — |
| $380001 | REG_POUTPUT | — | 手柄端口输出 |
| $380011 | REG_CRDBANK | — | 记忆卡 bank |
| $380021 | REG_SLOT | — | 选择卡槽（多槽 MVS） |
| $380031 | REG_LEDLATCHES | — | LED / 跑马灯锁存 |
| $380041 | REG_LEDDATA | — | 投币显示数据 |
| $380051 | REG_RTCCTRL | — | RTC 控制位 |
| $380061–$3800E7 | REG_RESETCC*/SETCC*/… | — | 投币计数器、投币锁定 |

### 2.3 系统寄存器（$3A0000–$3A001F，只写，奇地址，数据无关，地址 bit 4 选状态）

| 地址对 | 名称 | 功能 |
|---|---|---|
| $3A0001 / $3A0011 | REG_NOSHADOW / REG_SHADOW | 正常输出 / 整屏变暗 |
| $3A0003 / $3A0013 | REG_SWPBIOS / REG_SWPROM | 向量表来自 BIOS / 来自卡带 |
| $3A0005 / $3A0015 | REG_CRDUNLOCK1 / REG_CRDLOCK1 | 记忆卡写使能 / 禁止 |
| $3A000B / $3A001B | REG_BRDFIX / REG_CRTFIX | fix 图形与 Z80 代码来自主板 SFIX/SM1 / 来自卡带 S/M1 |
| $3A000D / $3A001D | REG_SRAMLOCK / REG_SRAMUNLOCK | Backup RAM 写保护 / 解除（MVS） |
| $3A000F / $3A001F | REG_PALBANK0 / REG_PALBANK1 | 切换 palette bank 0 / 1 |

### 2.4 LSPC 寄存器（$3C0000–$3C000F，16-bit，只能偶地址）

| 地址 | 名称 | 读 | 写 |
|---|---|---|---|
| $3C0000 | REG_VRAMADDR | 读 VRAM（地址不变） | 设置 VRAM 地址 |
| $3C0002 | REG_VRAMRW | 读 VRAM（地址不变） | 写 VRAM，写后地址 += REG_VRAMMOD |
| $3C0004 | REG_VRAMMOD | 读模数 | 设置有符号模数（自动增量） |
| $3C0006 | REG_LSPCMODE | bits 15:7 当前扫描线、bit 3 50/60 Hz（LSPC2）、bits 2:0 自动动画计数 | bits 15:8 自动动画速度、bit 7/6/5 定时器重载模式、bit 4 定时器中断使能、bit 3 禁止自动动画 |
| $3C0008 | REG_TIMERHIGH | — | 定时器重载值高 16 bit |
| $3C000A | REG_TIMERLOW | — | 定时器重载值低 16 bit |
| $3C000C | REG_IRQACK | — | bit 2 应答 VBL、bit 1 应答 timer、bit 0 应答 IRQ3 |
| $3C000E | REG_TIMERSTOP | — | bit 0：PAL 模式下前后 16 行停止定时器 |

VRAM 访问时序（由 LSPC 仲裁，68000 在渲染期间可随时访问，但不能太快）：设置地址后至少等 16 个 CPU 周期再读；
写数据后至少等 12 个 CPU 周期再写、16 个周期再改地址。跨越 $7FFF/$8000 边界时必须重新写 REG_VRAMADDR，不能靠模数自增。

### 2.5 中断

| 级别 | 来源 | 自动向量 | 说明 |
|---|---|---|---|
| IRQ1 | **VBlank** | $64 | 每帧一次，帧渲染即将开始时由 LSPC 发出 |
| IRQ2 | **Timer** | $68 | LSPC 内 32-bit 递减计数器（6 MHz 像素时钟），用于光栅效果 |
| IRQ3 | 冷启动 / reset | $6C | 上电后挂起，由 BIOS 处理 |

- 三个中断都由 LSPC 产生（IPL0/IPL1 线）；必须写 REG_IRQACK 应答，否则不会再次触发。`move.w #7, REG_IRQACK` 一次应答全部。
- Timer 模式（REG_LSPCMODE）：bit 5 写 REG_TIMERLOW 时立即重载；bit 6 每帧第一条 vblank 行的 hblank 重载一次；bit 7 归零时自动重载（周期性）。
  重载值 ≥ 4，否则 68000 会被中断淹没。
- Neo Geo CD 把 VBL 和 timer 的级别对调（IRQ1 timer，IRQ2 VBL）——阅读旧资料时注意。

### 2.6 Watchdog

- 集成在 NEO-B1 中，由 CHBL（水平消隐）信号计数。68000 必须周期性写 REG_DIPSW（$300001）"踢狗"，通常在 VBL 中断处理程序中做。
- 超时后 /RESET 拉低约 8 帧、再拉高约 20 帧，整机复位；若程序一直不踢，系统以约 3.7 Hz 反复复位（著名的 "click of death" 咔哒声）。
- 实测超时约 0.13 s（MAME 实现）；NEO-B1 的 DOGE 引脚接地可物理禁用。

---

## 3. Z80 音频子系统

### 3.1 内存映射

| 起始 | 结束 | 大小 | 内容 |
|---|---|---|---|
| $0000 | $7FFF | 32 KiB | M1 ROM 固定段（前 32 KiB） |
| $8000 | $BFFF | 16 KiB | bank 窗口 3（port $0B） |
| $C000 | $DFFF | 8 KiB | bank 窗口 2（port $0A） |
| $E000 | $EFFF | 4 KiB | bank 窗口 1（port $09） |
| $F000 | $F7FF | 2 KiB | bank 窗口 0（port $08） |
| $F800 | $FFFF | 2 KiB | Z80 工作 RAM |

M1 ROM 最大 4 MiB（通过 bank 窗口访问）。Z80 代码可以来自卡带 M1 ROM，也可以被 REG_BRDFIX 切换到主板上的 SM1 ROM（BIOS 自带的音效驱动，用于系统设置菜单）。

### 3.2 端口映射

| 端口 | 读 | 写 |
|---|---|---|
| $00 | 读取 68000 写入的命令字节，**同时应答 NMI** | 把命令锁存清零（$00） |
| $04 | YM2610 状态（寄存器组 A 地址） | 写 YM2610 寄存器地址 A |
| $05 | YM2610 数据 A | 写 YM2610 数据 A |
| $06 | YM2610 地址 B（ADPCM 等） | 写地址 B |
| $07 | YM2610 数据 B | 写数据 B |
| $08 | **读 = 设置 $F000 窗口的 bank** | 使能 NMI |
| $09 | 读 = 设置 $E000 窗口的 bank | — |
| $0A | 读 = 设置 $C000 窗口的 bank | — |
| $0B | 读 = 设置 $8000 窗口的 bank | — |
| $0C | （未使用） | **写应答字节给 68000** |
| $18 | 同 $08–$0B | 禁止 NMI |
| $C0 | port $00 镜像 | port $00 镜像 |

**bankswitch 靠"读端口"而非"写端口"**：Z80 执行 `in a,(c)` 时把 B 寄存器放在地址总线高 8 位、C 放在低 8 位，
NEO-ZMC 芯片从地址高位捕获 bank 号，从而省掉数据总线引脚。所以典型写法是 `ld bc, (bank<<8)|$0B ; in a,(c)`。上电时 bank 未定义，Z80 必须自己初始化。

### 3.3 Z80 中断

| 中断 | 向量 | 来源 |
|---|---|---|
| NMI | $0066 | 68000 写 REG_SOUND（需先通过 port $08 使能） |
| IRQ (IM1) | $0038 | YM2610 的 Timer A / Timer B，音效驱动用它来做节拍 |

---

## 4. YM2610（OPNB）

| 发生器 | 通道数 | 说明 |
|---|---|---|
| FM（OPN 4-op） | 4（YM2610B 为 6） | 旋律、音色合成 |
| SSG（PSG 方波/噪声） | 3 | 简单音效 |
| ADPCM-A | 6 | 4-bit 编码、12-bit 播放，**固定约 18.5 kHz**；每段样本 256 B–1 MiB，256 B 对齐，不能跨 1 MiB 页 |
| ADPCM-B | 1 | 4-bit 编码、16-bit 播放，采样率可变约 1.85–55.5 kHz（Delta-N 寄存器） |

- 时钟 8 MHz，由 LSPC 的 8M 输出提供。
- ADPCM 样本由 YM2610 **直接**从卡带 PROG board 上的 V ROM 读取（独立的 ADPCM 总线，早期游戏 ADPCM-A/B 分别放在 V1/V2 ROM，
  后期用 PCM 复用芯片共用一组 ROM）。Z80 只设置起止地址和触发播放，不搬运样本数据——这是 Neo Geo 里唯一类似"DMA"的数据通路。
- FM/ADPCM 数字输出经外部 YM3016 DAC 转为模拟信号；SSG 为芯片直接模拟输出；三者在板上混音。
- 两个硬件定时器可向 Z80 发 IRQ。

---

## 5. 68000 ↔ Z80 通信：sound latch

两颗 CPU 之间**没有共享内存**。唯一的通道是 NEO-C1 中的两个 8-bit 锁存器（mailbox）和一根 NMI 线：

```
   68000                       NEO-C1 (latch)                    Z80
     │  move.b #cmd, REG_SOUND     │                               │
     │────────────────────────────►│ cmd latch = cmd               │
     │                             │──────── /NMI ────────────────►│  (若 port $08 已使能)
     │                             │                               │ NMI 向量 $0066
     │                             │        in a,($00)             │
     │                             │◄──────────────────────────────│  读命令，同时应答 NMI
     │                             │        out ($00),a   (可选)   │  清零命令锁存
     │                             │                               │  ... 处理：触发音效/音乐 ...
     │                             │        out ($0C),a            │
     │                             │ reply latch = cmd | $80       │◄── 约定：回显命令并置 bit 7
     │  move.b REG_SOUND, d0       │                               │
     │◄────────────────────────────│ 读到 reply（不产生中断）        │
     │  轮询直到 reply == cmd|$80  │                               │
```

要点：

- **方向不对称**：68000→Z80 有中断（NMI），Z80→68000 没有中断，68000 只能轮询 REG_SOUND。
- 命令含义完全由卡带上的 Z80 驱动定义，只有三个命令被 BIOS 约定：
  - `$01` 准备切换卡槽（Z80 停止所有声音、使能中断、回复 port $0C、在 RAM 中空转；MVS 上电若不及时回复会显示 "Z80 ERROR"）；
  - `$02` 播放 eyecatcher 音乐（不需回复）；
  - `$03` Z80 软复位（100 ms 内完成，不需回复）。
- 典型音效驱动把 bit 7 置位的回显作为 ACK；复杂驱动用多字节序列（例如先发"参数"再发"触发"）。
- 因为只有一个字节深度，68000 发送过快会**覆盖**尚未被读取的命令。所以通常每帧（VBL）只发一个命令，或轮询 ACK 后再发下一个。

这正是 `examples/assembly/neogeo_mailbox.s` 在 Ripes 上用单核 RISC-V 模拟的协议：一个内存字当 cmd latch，一个当 reply latch，
"sound" 子程序模拟被 NMI 唤醒的 Z80。

---

## 6. LSPC 视频系统

### 6.1 显示时序

```
            ◄──────────── 384 px (1536 mclk, 64 µs) ─────────────►
            ◄─ 320 px 可见 ─► 8 fp │ 28 sync │ 28 bp
         ┌───────────────────────────────────────┐  ─┐
         │        上消隐边框 (NTSC 不显示)         │   │
         ├───────────────────────────────────────┤   │ 264 行 / 帧
         │                                       │   │ (15.625 kHz ÷ 264 = 59.19 Hz)
         │   224 行可见 (fix 28 tile 行)          │   │
         │                                       │   │
         ├───────────────────────────────────────┤   │
         │   下消隐边框 + VBlank → IRQ1           │   │
         └───────────────────────────────────────┘  ─┘
   (264 − 224 = 40 行不可见，分布在上下边框与垂直消隐；精确的行号分配见 wiki "Display timing")
```

- 像素时钟 6 MHz，一个像素 4 mclk。水平 384 px 中 320 px 可见（多数街机显示器两侧各裁掉 8 px，SNK 建议安全区 304×224）。
- NTSC 264 行中 224 行可见；PAL 312 行、256 行可见。
- VBlank 开始时 LSPC 发出 IRQ1。游戏在这个窗口里更新 VRAM / palette（虽然 LSPC 允许随时访问，但 palette RAM 在活动显示期间写入会出现小 glitch，因为 NEO-B1 总是把总线优先给 68000）。

### 6.2 Sprite 模型

| 项目 | 值 |
|---|---|
| Tile | 16×16 px，4 bpp 平面格式，128 B；颜色 0 透明 |
| Sprite 形状 | **1 tile 宽（16 px）、1–32 tile 高（最高 512 px）的竖条** |
| Sprite 数量 | VRAM 中有 448 个槽，硬件每帧**只显示 381 个** |
| 每扫描线 | **最多 96 个 sprite**，超出的不画 |
| Tile 编号 | 20 bit → 最多 1,048,576 tile（128 MiB C ROM），再多要 bankswitch（NEO-CMC） |
| 缩放 | 只能缩小（shrink），不能放大；逐像素抽样，无滤波 |
| 坐标 | 以 512 px 为周期环绕（X 和 Y 都是 9 bit） |

Neo Geo **没有背景层**（tile map）：背景、前景、UI 边框全部由 sprite 竖条拼成。一个 320 px 宽的背景 = 20 个相邻的 sprite，
通过 sticky bit 链成一组，只要移动第一个就能整体滚动。

#### Sprite Control Blocks（SCB，均在 VRAM 中）

| 块 | VRAM 地址 | 每 sprite | 内容 |
|---|---|---|---|
| SCB1 | $0000–$6FFF | 64 word | tile map：32 对 word，从上到下每 tile 一对 |
| SCB2 | $8000–$81FF | 1 word | shrink：bits 11:8 水平（$F 全宽 16 px，$0 为 1 px）；bits 7:0 垂直（$FF 全高，$00 为 1/256） |
| SCB3 | $8200–$83FF | 1 word | bits 15:7 Y（屏幕 y = 496 − Y）；bit 6 **sticky**；bits 5:0 高度（tile 数，0–32；33 = 32 tile 高且缩小时边界循环） |
| SCB4 | $8400–$85FF | 1 word | bits 15:7 X（从屏幕左边算起） |

SCB1 每个 tile 的两个 word：

```
偶 word : tile number bits 15:0
奇 word : 15      8 7    4 3   2   1   0
          ├ palette ┼ tile ┼AA8┼AA4┼ V ┼ H ┤
          │ (0-255) │ 19:16│   │   │flip│flip│
```

- 自动动画（auto-animation）：AA8=1 时硬件把 tile 编号低 3 bit 替换成全局 3-bit 帧计数（8 帧循环），AA4=1 替换低 2 bit（4 帧）。
  速度由 REG_LSPCMODE bits 15:8 决定（n → 每 n+1 帧前进一次）。用于水面、火焰等，不耗 CPU。
- 从 68000 的角度，更新一个 sprite = 向 REG_VRAMADDR 写 SCB 地址，再向 REG_VRAMRW 连续写 word（配合 REG_VRAMMOD 自增）。

#### Sticky bit（chaining）

SCB3 bit 6 = 1 时，该 sprite "粘"到左边那个 sprite 上：

- 继承前一个 sprite 的 **Y 坐标、高度、垂直 shrink**；自己的 Y/高度/垂直 shrink 字段被忽略。
- X 自动排在前一个 sprite 右侧（前一个的宽度由其水平 shrink 决定）。
- **水平 shrink 不继承**，每个链内 sprite 各自设置；这样整组缩放时每根竖条宽度可以独立控制。
- 修改"驱动" sprite 即可移动整条链，VRAM 中被驱动 sprite 的值不变。

#### Shrink

- 水平：4-bit 值 h，输出 h+1 个像素列，LSPC 内部有固定的"抽列"模式。
- 垂直：8-bit 值 v，LSPC 查主板上的 **L0 ROM**（lookup table）决定每条线取 tile 中的哪一行。缩小后的图形仍画在 SCB3 定义的"显示窗口"里；
  窗口高于缩小后图形时，剩余行重复第 15 号 tile 的最后一行（SNK 建议把那一行留透明）。

### 6.3 Fix layer

| 项目 | 值 |
|---|---|
| Tile | 8×8 px，4 bpp，32 B；颜色 0 透明；只能用 **前 16 个 palette** |
| 地图 | VRAM $7000–$74FF，40 列 × 32 行 = 1280 word，**列主序**（地址 = $7000 + x×32 + y） |
| 可见 | NTSC 只显示 28 行（顶部 2 行、底部 2 行隐藏）；多数游戏也不用最左/最右列（安全区 38×28） |
| Word 格式 | bits 15:12 palette（0–15），bits 11:0 tile 编号（0–4095 → S ROM 128 KiB，NEO-CMC 可 bank 扩展） |
| 优先级 | **永远在所有 sprite 之上**，不能被遮挡 → 用于分数、血条、文字 |

Fix 图形来源可由 REG_BRDFIX/REG_CRTFIX 在主板 SFIX ROM（BIOS 界面字体）与卡带 S ROM 之间切换。

### 6.4 VRAM 布局

```
 lower VRAM (64 KiB, 32K words, 2×62256 SRAM, "慢"总线 B0~B14/E0~E15)
 $0000 ┌──────────────────────────────────────────┐
       │ SCB1  sprite tile maps  448 × 64 words    │
 $6FFF ├──────────────────────────────────────────┤
 $7000 │ Fix map  40 × 32 words                   │
 $74FF ├──────────────────────────────────────────┤
 $7500 │ extension (NEO-CMC fix bankswitch 表)     │
 $7FFF └──────────────────────────────────────────┘
 upper VRAM (4 KiB, 2K words, 2×2K×8 SRAM, "快"总线 C0~C10/F0~F15)
 $8000 ┌──────────────────────────────────────────┐
       │ SCB2  shrink            512 words         │
 $8200 │ SCB3  Y / sticky / size 512 words         │
 $8400 │ SCB4  X                 512 words         │
 $8600 │ 偶数扫描线 active sprite list  128 words   │ ◄── LSPC 自己写
 $8680 │ 奇数扫描线 active sprite list  128 words   │ ◄── LSPC 自己写
 $8700 │ 未用                     256 words        │
 $87FF └──────────────────────────────────────────┘
```

两块 VRAM 接在 LSPC 的**两条独立总线**上，可以并行读取：渲染一条线时，LSPC 同时在"快"VRAM 里扫描 SCB3 判断哪些 sprite 与下一行相交，
在"慢"VRAM 里为当前行取 SCB1 的 tile 编号。VRAM 里 **没有任何像素数据**。

### 6.5 Palette RAM 与颜色

- 2 个 bank（REG_PALBANK0/1 切换，同一时刻只能用一个），每 bank 256 palette × 16 色 × 16-bit = 8 KiB，映射在 $400000–$401FFF。
- 每个 palette 的颜色 0 是透明索引；sprite 可用全部 256 个 palette，fix 只能用前 16 个。
- $400000（palette 0 颜色 0）必须是纯黑 `$8000`，作为视频基准电平；$401FFE（palette 255 颜色 15）是 **backdrop 色**——NEO-B1 的行缓冲每行先清成 $FFF，所以没有任何东西覆盖的像素显示这个颜色。

16-bit 颜色字：

```
 15   14  13  12  11      8  7       4  3       0
 ┌───┬───┬───┬───┬──────────┬──────────┬──────────┐
 │Drk│R0 │G0 │B0 │ R4..R1   │ G4..G1   │ B4..B1   │
 └───┴───┴───┴───┴──────────┴──────────┴──────────┘
```

每个分量 5 bit（R0/G0/B0 是最低位），再加一个三分量共享的 "dark bit"（bit 15，整体压暗），共 65536 个可表示值（实际约 2^16 中有重复），
同屏最多 4096 色（256 palette × 16 − 透明）。REG_SHADOW 可以让整屏再压暗一级（用于暂停/对话）。

---

## 7. 一帧是怎样画出来的（无帧缓冲的逐行合成）

Neo Geo 没有 framebuffer，也没有"把所有 sprite 画进一张图再扫出"的阶段。每条扫描线都是**实时**生成的：

```
   帧开始 (VBL, IRQ1)
   68000: 踢 watchdog → 读输入 → 游戏逻辑 → 写 SCB1..4 / fix map / palette → 发 sound cmd → 等下一次 VBL
          ───────────────────────────────────────────────────────────────────────
   LSPC, 对每条扫描线 n:
     [行 n-1 期间]   在快 VRAM 中顺序扫描 SCB3[0..380]：
                     若 sprite s 的 Y 窗口 (Y, size) 覆盖行 n → 追加到 list[n 奇偶]，最多 96 个
     [行 n 的 hblank + 活动期间]  对 list 中每个 sprite（按编号顺序，后者覆盖前者）:
                     ① 由 (行 n − Y, 垂直 shrink) 经 L0 ROM 得到 tile 索引 t 与 tile 内行 r
                     ② 读 SCB1[s][t] → tile number (20 bit) + palette + flip + auto-anim
                     ③ 在 P bus 上输出 C ROM 地址 (tile, r)；PK1 锁存 → CHA board 的 C ROM 输出 32 bit (左/右 8 px 的 4 个 bitplane)
                     ④ NEO-ZMC2 / PRO-CT0 串行化成 16 个像素 (4-bit 索引 + 透明标志)，按水平 shrink 抽列、按 H flip 反转
                     ⑤ NEO-B1 把不透明像素写进当前行缓冲 (palette<<4 | index)，透明像素跳过
                     ⑥ 对 fix layer：取 fix map 该行的 40 个 word → S ROM 地址 (PK2) → 8 px，写入行缓冲 (覆盖 sprite)
     [行 n 输出期间]  NEO-B1 以 6 MHz 从另一个行缓冲逐像素读出 12-bit palette 地址 → palette RAM → 16-bit 颜色 → 锁存 → DAC → RGB
                      （行缓冲成对交替：一个在写，一个在读）
   最后 40 行 (不可见): 垂直边框 + VBlank，LSPC 拉 IRQ1，循环回到帧开始
```

几个值得注意的细节：

- **优先级 = 编号顺序**：sprite 380 画在 sprite 0 上面，因为后写入行缓冲的覆盖先写的。fix 最后写，所以永远在最上层。
- **96/行限制**来自带宽（粗略估算）：每条线 1536 mclk，每个 sprite 一行要取 2 次 C ROM（左右 8 px）+ 读 SCB1，1536 ÷ 96 = 16 mclk/sprite。超过的 sprite 被丢弃而不是减速（硬件不会"掉帧"，只会"掉 sprite"）。
- **68000 可以在渲染中改 VRAM**，LSPC 负责仲裁；但 active sprite list 是 LSPC 在前一行构建的，所以改动在下一行才生效——光栅效果（timer IRQ）就是利用这一点在特定行修改 SCB。
- **C ROM 访问时间必须 < 250 ns（7 mclk）**，这是卡带 ROM 的硬性要求，也是"ROM 直接当纹理内存"的代价。

---

## 8. 卡带：ROM 类型与两块板子

一个 Neo Geo 卡带里有两块 PCB，各自通过独立的边缘连接器插到主板，**分别挂在不同的总线上**：

```
  ┌─────────────────────────── 卡带外壳 ───────────────────────────┐
  │  PROG board（上）                     CHA board（下）           │
  │  ┌────────────────────────┐          ┌──────────────────────┐  │
  │  │ P ROM  68000 程序       │          │ C ROM ×2n  sprite 图形│  │
  │  │ V ROM  ADPCM 采样       │          │ S ROM      fix 图形   │  │
  │  │ (bank 锁存 / PCM 复用)  │          │ M1 ROM     Z80 程序   │  │
  │  └───┬──────────────┬─────┘          │ NEO-ZMC(2)/NEO-273/   │  │
  │      │              │                │ NEO-CMC   地址锁存+   │  │
  │      │              │                │           串行化+bank │  │
  │      │              │                └──┬─────────┬─────┬────┘  │
  └──────┼──────────────┼───────────────────┼─────────┼─────┼───────┘
     68000 bus      YM2610 ADPCM bus     P bus (LSPC)  FIX bus   Z80 bus
     (A1-A23,D0-D15)  (RA/RB 24-bit)     24-bit 复用    8-bit    (SDA/SDD)
```

| ROM | 内容 | 读取者 | 所在板 | 典型大小 |
|---|---|---|---|---|
| **P** (Program) | 68000 代码 + 数据 | 68000 | PROG | 1–8 MiB（>2 MiB 用 PORT 区 bankswitch） |
| **V** (Voice) | ADPCM-A / ADPCM-B 采样 | YM2610（直接） | PROG | 1–16 MiB |
| **M1** (Music) | Z80 音效驱动 + 乐谱数据 | Z80 | CHA | 64 KiB–4 MiB（bank 窗口） |
| **S** (fiX) | fix layer 8×8 tile | LSPC/NEO-B1 | CHA | 128 KiB |
| **C** (Character) | sprite 16×16 tile，成对（C1/C2…）各存 2 个 bitplane | LSPC（经串行化器） | CHA | 数 MiB–90+ MiB |

- C ROM 成对出现：奇数号（C1, C3…）存 bitplane 0/1，偶数号（C2, C4…）存 bitplane 2/3；LSPC 一次地址同时从一对 ROM 取 32 bit = 8 px × 4 bpp。
  每 tile 128 B，按 右上/右下/左上/左下 四个 8×8 块存放。
- CHA board 上的 **NEO-273** 锁存 P bus 上的 C/S ROM 地址（PK1/PK2），**NEO-ZMC2** = Z80 bank 控制器 (ZMC) + C ROM 像素串行化器 (PRO-CT0) 二合一，
  后期的 **NEO-CMC** 进一步集成并加入图形加扰（防盗版）和 fix/tile bankswitch。
- 68000 读 P ROM 的路径：68000 地址总线 → 主板 NEO-E0 地址译码 → PROG 连接器 → P ROM → 数据总线回 68000。ROMWAIT/PWAIT 可插入等待周期以适应慢 ROM。
- 因为三条总线物理分离，卡带可以同时被 68000（P）、YM2610（V）、LSPC（C/S）和 Z80（M1）**并行**访问，互不争用。

---

## 9. 与现代 SoC 的对照

| 维度 | Neo Geo（1990） | 现代 SoC / GPU（见 [gpu.md](gpu.md)） | PS3 Cell（见 [ps3_cell.md](ps3_cell.md)） |
|---|---|---|---|
| 帧缓冲 | **无**。逐行合成，行缓冲 ×4 | 有，多级（tile cache + 帧缓冲在 DRAM） | RSX 有帧缓冲（GDDR3） |
| 像素数据存放 | ROM 直连显示芯片，CPU 不可见 | 纹理在统一 DRAM，CPU/GPU 都能访问 | XDR/GDDR3 分离，但通过 FlexIO 互访 |
| 数据搬运 | **无 DMA**，68000 通过 3 个 MMIO 寄存器逐 word 写 VRAM | DMA 引擎、命令队列 | MFC DMA（SPE 显式搬运） |
| CPU↔协处理器 | 1 字节 mailbox + NMI（单向中断） | 门铃 + 描述符环 + 中断/轮询 | mailbox 4+1 + signal notification + DMA |
| 并行性 | 异构多 CPU（68000 + Z80），固定分工 | SIMT：成千上万线程同一程序 | 1 PPE + 8 SPE，SPMD/流水 |
| 图形原语 | 固定功能：sprite 竖条、shrink、fix | 可编程 shader | 可编程 vertex/pixel shader |
| 带宽瓶颈的表现 | 每线 96 sprite 硬上限，超出则丢弃 | 降帧率 | 降帧率 |
| 内存一致性 | 不存在问题（无 cache，无共享） | cache flush / fence | SPE Local Store 显式管理 |
| 时序确定性 | 完全确定（每行 1536 mclk） | 不确定 | 基本确定（SPE 无 cache） |

启发：

1. **"无帧缓冲"不是缺陷而是取舍**——1990 年 DRAM 昂贵，把像素留在 ROM 里、只缓存一行，让 Neo Geo 能用 4 KiB 的快 VRAM 驱动 381 个 sprite。
   现代 tile-based GPU（移动端 TBDR）其实回到了类似思路：按 tile 合成，减少对外部 DRAM 的访问。
2. **mailbox 协议**是所有"主 CPU + 协处理器"系统的最小公分母。Cell 的 SPE mailbox、GPU 的门铃寄存器、NPU 的描述符环，都是这个 1 字节锁存的放大版——
   增加了深度（FIFO）、双向中断、以及通过共享内存传递大块数据。
3. **带宽决定上限**：96 sprite/行 和 GPU 的 occupancy、Cell 的 EIB 带宽一样，都是"每单位时间能取多少数据"的直接体现。
4. **分离总线 vs 统一内存**：Neo Geo 的三条总线把争用问题在硬件上消灭了，代价是 CPU 不能生成/修改图形（没有程序化纹理、没有软件渲染）。
   现代 SoC 选择统一内存 + cache + 仲裁，换来灵活性。

---

## 10. 常见误区

| 误区 | 事实 |
|---|---|
| "Neo Geo 有背景层/tilemap 层" | 只有 sprite 和 fix layer。所有背景都是 sprite 竖条链 |
| "Sprite 是 16×16 的" | tile 是 16×16；**sprite 是 16 px 宽、最高 512 px 的竖条** |
| "VRAM 存图形" | VRAM 只存属性和索引；图形永远在 C/S ROM 里，68000 看不到 |
| "一帧可以画 448 个 sprite" | VRAM 有 448 个槽，但硬件每帧只处理 381 个，每行最多 96 个 |
| "Sprite 可以放大" | 只能缩小（shrink），且是无滤波抽样 |
| "VBL 是 IRQ2" | 卡带系统 VBL = IRQ1（向量 $64），timer = IRQ2（$68）；Neo Geo CD 对调 |
| "Z80 通过写端口切 bank" | 通过**读**端口 $08–$0B 切 bank，bank 号放在地址总线高 8 位（B 寄存器） |
| "读 port $00 会清除命令" | 读 port $00 取命令并应答 NMI；**写** port $00 才把锁存清零 |
| "Z80 回复会中断 68000" | 不会，68000 只能轮询 REG_SOUND |
| "Z80 搬运采样数据给 YM2610" | YM2610 自己从 V ROM 读 ADPCM，Z80 只写寄存器 |
| "M1 ROM 在 PROG board 上" | M1 在 **CHA board**（和 C/S ROM 一起），PROG board 放 P 和 V ROM |
| "68000 写 VRAM 必须等 VBlank" | LSPC 随时仲裁，可在活动显示期间写（只需遵守 12/16 周期间隔）；palette RAM 在活动期间写会有轻微 glitch |
| "Watchdog 是软件功能" | 硬件在 NEO-B1 中，不踢就整机复位 |
| "颜色是 15-bit RGB" | 5+5+5 bit 加一个共享 dark bit，16-bit 字；$400000 必须是 $8000（黑） |
| "分辨率 304×224" | 硬件输出 320×224，304 是多数显示器可见/安全区 |

---

## 11. 参考资料

- NeoGeo Development Wiki：https://wiki.neogeodev.org（68k memory map、Memory mapped registers、Z80 port map、Z80 bankswitching、
  VRAM、Sprites、Sticky bit、Sprite shrinking、Fix layer、Palettes、Colors、Display timing、68k interrupts、Timer interrupt、
  Watchdog、YM2610、ADPCM、LSPC2-A2、NEO-B1、NEO-ZMC2、Sprite graphics format、Cartridges、PROG board、CHA board、Bankswitching）
- MAME `src/mame/snk/neogeo.cpp`（watchdog、中断时序的实现参考）
- 本仓库：`resources/arch/neogeo.json`（拓扑 + 3 个 flow），`examples/assembly/neogeo_mailbox.s`（mailbox 协议模拟）

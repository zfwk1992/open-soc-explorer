# ---------------------------------------------------------------------------
# neogeo_mailbox.s  --  Neo Geo 68000 <-> Z80 "sound latch" 协议模拟
#                       Neo Geo 68000 <-> Z80 sound-latch (mailbox) protocol model
#
# 真实硬件 / Real hardware (see docs/arch/neogeo.md, section 5):
#   * 68000 写 REG_SOUND ($320000) -> NEO-C1 的 8-bit 命令锁存，并向 Z80 发 NMI。
#     68000 writes REG_SOUND -> 8-bit command latch in NEO-C1, Z80 gets an NMI.
#   * Z80 在 NMI 处理程序里 in a,($00) 读命令（同时应答 NMI），out ($00),a 清锁存。
#     Z80 NMI handler reads port $00 (also acks the NMI), out ($00) clears the latch.
#   * Z80 处理完后 out ($0C),a 写应答锁存，约定值 = cmd | $80。
#     Z80 replies through port $0C, convention: echo the command with bit 7 set.
#   * 68000 读 REG_SOUND 得到应答；没有中断，只能轮询。
#     68000 polls REG_SOUND for the reply; there is no interrupt in that direction.
#   * 锁存深度只有 1 字节：连续两次写而 Z80 没来得及读，前一条命令就丢了。
#     The latch is one byte deep: two back-to-back writes lose the first command.
#
# 本程序在单核 RISC-V 上用协作式多任务模拟这两颗 CPU：
# This program models both CPUs with cooperative multitasking on one RISC-V core:
#   main_task  = 68000 侧（按脚本发命令、轮询应答）  / the 68000 side
#   sound_task = Z80 侧（NMI 处理 + 软件 FIFO + 处理 + 应答） / the Z80 side
#   cmd_latch / reply_latch / nmi_line = NEO-C1 中的硬件锁存与 NMI 线
#
# 运行方法 / How to run:
#   1. 在 I/O tab 中实例化一个 "LED Matrix"（默认 35x25 即可，至少 12 列 x 8 行）。
#      Instantiate an "LED Matrix" peripheral in the I/O tab (default size is fine).
#   2. 运行。控制台打印协议日志；LED 矩阵显示：
#      Run. The console logs the protocol; the LED matrix shows:
#        row 1 : cmd_latch   的 8 个 bit（黄）  / command latch bits (yellow)
#        row 3 : reply_latch 的 8 个 bit（青）  / reply latch bits (cyan)
#        row 5 : col 2 NMI 挂起（红）  col 5 NMI 使能（绿）
#                col 8 Z80 忙（蓝）    col 11 68000 等待应答（紫）
#        row 7 : Z80 软件 FIFO 占用（白） / Z80 software FIFO occupancy (white)
#
# 只使用 RV32I 指令。 RV32I only.
# ---------------------------------------------------------------------------

.data
# --- 模拟 NEO-C1 硬件 / simulated NEO-C1 hardware -------------------------
cmd_latch:    .word 0        # REG_SOUND write side  (68000 -> Z80)
reply_latch:  .word 0        # REG_SOUND read side   (Z80 -> 68000), port $0C
nmi_line:     .word 0        # 1 = Z80 /NMI asserted
nmi_enabled:  .word 0        # Z80 out ($08) = 1 enable, out ($18) = 0 disable

# --- Z80 驱动的软件 FIFO（在 Z80 RAM $F800 里）/ Z80 driver's software FIFO -
fifo:         .word 0, 0, 0, 0
fifo_head:    .word 0        # 读指针 / read index
fifo_tail:    .word 0        # 写指针 / write index
fifo_count:   .word 0

# --- 68000 侧的命令脚本 / command script for the 68000 side ----------------
# 每条 2 个 word: (cmd, flags)。flags bit0 = 等待应答, bit1 = burst（与下一条连发，不让出 CPU）
# Two words per entry: (cmd, flags). bit0 = wait for ack, bit1 = burst with next entry.
script:
    .word 0x03, 0           # $03: Z80 soft reset, no reply expected
    .word 0x01, 1           # $01: prepare slot switch, BIOS waits for the reply
    .word 0x20, 1           # play SFX, wait for ack
    .word 0x21, 0           # play SFX, fire and forget
    .word 0x22, 2           # burst: $22 then $23 with no chance for the Z80 to read
    .word 0x23, 1           #   -> $22 is lost (overwritten in the 1-byte latch)
    .word 0x7F, 1           # last command
    .word -1,   0           # end of script

str_boot:    .string "Z80 : boot, out ($08) -> NMI enabled\n"
str_send:    .string "68k : move.b #$"
str_send2:   .string ", REG_SOUND\n"
str_burst:   .string "68k : (burst, no yield)\n"
str_nmi:     .string "Z80 : NMI -> in a,($00) = $"
str_full:    .string "Z80 : FIFO full, command dropped\n"
str_proc:    .string "Z80 : processing $"
str_reply:   .string "Z80 : out ($0C) <- $"
str_noreply: .string "Z80 : no reply for this command\n"
str_ack:     .string "68k : REG_SOUND == $"
str_ackok:   .string " (ack)\n"
str_timeout: .string "68k : timeout waiting for reply -> Z80 ERROR\n"
str_done:    .string "--- script finished ---\n"
str_nl:      .string "\n"

.text
# 寄存器分配 / register allocation
#   s0 = LED_MATRIX_0_BASE          s1 = row stride in bytes (WIDTH*4)
#   s2 = 68000 state (0 idle, 1 waiting for ack)
#   s3 = 68000 current command      s4 = script pointer      s5 = wait counter
#   s6 = Z80 busy countdown         s7 = Z80 command being processed
#   s8 = Z80 "no reply" flag

main:
    li   s0, LED_MATRIX_0_BASE
    li   s1, LED_MATRIX_0_WIDTH
    slli s1, s1, 2                  # bytes per LED row
    li   s2, 0
    la   s4, script
    li   s6, 0
    li   s8, 0

    jal  sound_init                 # Z80 上电初始化 / Z80 power-on init

main_loop:
    jal  main_task                  # 68000 跑一小段 / run the 68000 for a slice
    jal  sound_task                 # Z80 跑一小段   / run the Z80 for a slice
    jal  draw_leds
    jal  delay
    j    main_loop

# ===========================================================================
# 68000 侧 / 68000 side
# ===========================================================================
main_task:
    addi sp, sp, -4
    sw   ra, 0(sp)
    bnez s2, mt_wait

mt_send:
    lw   t0, 0(s4)                  # cmd
    bltz t0, mt_script_done
    lw   t1, 4(s4)                  # flags
    addi s4, s4, 8
    mv   s3, t0
    mv   s9, t1                     # keep flags across calls
    mv   a0, s3
    jal  hw_write_reg_sound         # move.b d0, REG_SOUND
    la   a0, str_send
    jal  print_str
    mv   a0, s3
    jal  print_hex
    la   a0, str_send2
    jal  print_str
    andi t2, s9, 2
    beqz t2, mt_check_wait
    la   a0, str_burst              # burst: 立即发下一条，不让 Z80 有机会读
    jal  print_str                  # the Z80 never gets to read the first byte
    j    mt_send
mt_check_wait:
    andi t2, s9, 1
    beqz t2, mt_ret                 # fire and forget
    li   s2, 1                      # now waiting for ack
    li   s5, 0
    j    mt_ret

mt_wait:
    la   t0, reply_latch            # move.b REG_SOUND, d0
    lw   t0, 0(t0)
    ori  t1, s3, 0x80
    beq  t0, t1, mt_acked
    addi s5, s5, 1
    li   t2, 50                     # 轮询超时 / polling timeout (loop iterations)
    blt  s5, t2, mt_ret
    la   a0, str_timeout            # 真机上 BIOS 在此显示 "Z80 ERROR"
    jal  print_str
    li   s2, 0
    j    mt_ret
mt_acked:
    la   a0, str_ack
    jal  print_str
    ori  a0, s3, 0x80
    jal  print_hex
    la   a0, str_ackok
    jal  print_str
    li   s2, 0
    j    mt_ret

mt_script_done:
    la   a0, str_done
    jal  print_str
    jal  draw_leds
    li   a7, 10                     # exit
    ecall

mt_ret:
    lw   ra, 0(sp)
    addi sp, sp, 4
    ret

# --- 硬件行为：68000 写 REG_SOUND / hardware: 68000 writes REG_SOUND ---------
# a0 = command byte. 只使用 t5/t6。 Uses only t5/t6.
hw_write_reg_sound:
    andi a0, a0, 0xFF
    la   t5, cmd_latch
    sw   a0, 0(t5)                  # 命令锁存被无条件覆盖 / latch is overwritten unconditionally
    la   t5, nmi_enabled
    lw   t6, 0(t5)
    beqz t6, hw_wrs_ret             # Z80 没使能 NMI -> 命令静静地躺在锁存里
    la   t5, nmi_line
    li   t6, 1
    sw   t6, 0(t5)                  # 拉 /NMI
hw_wrs_ret:
    ret

# ===========================================================================
# Z80 侧 / Z80 side
# ===========================================================================
sound_init:
    addi sp, sp, -4
    sw   ra, 0(sp)
    la   t0, nmi_enabled            # out ($08),a  -> enable NMI
    li   t1, 1
    sw   t1, 0(t0)
    la   a0, str_boot
    jal  print_str
    lw   ra, 0(sp)
    addi sp, sp, 4
    ret

sound_task:
    addi sp, sp, -4
    sw   ra, 0(sp)

    # ---- (1) NMI 处理程序：读 port $00，推入 FIFO，应答 ---------------------
    #      NMI handler: in a,($00), push to FIFO, ack
    la   t0, nmi_line
    lw   t1, 0(t0)
    beqz t1, st_mainloop
    sw   zero, 0(t0)                # 读 port $00 同时应答 NMI / reading port $00 acks the NMI
    la   t0, cmd_latch
    lw   a0, 0(t0)                  # in a,($00)
    sw   zero, 0(t0)                # out ($00),a : 清零命令锁存 / clear the command latch
    mv   s10, a0
    la   a0, str_nmi
    jal  print_str
    mv   a0, s10
    jal  print_hex
    la   a0, str_nl
    jal  print_str
    mv   a0, s10
    jal  fifo_push                  # 真实驱动也只在 NMI 里做这么多，尽快退出
                                    # real drivers keep the NMI handler this short

    # ---- (2) Z80 主循环：处理 FIFO 中的命令，模拟耗时的 YM2610 寄存器写 -----
    #      Z80 main loop: process FIFO entries, model slow YM2610 register writes
st_mainloop:
    beqz s6, st_idle
    addi s6, s6, -1
    bnez s6, st_ret                 # still busy
    bnez s8, st_noreply
    ori  a0, s7, 0x80               # out ($0C),a : reply = cmd | $80
    la   t0, reply_latch
    sw   a0, 0(t0)
    mv   s10, a0
    la   a0, str_reply
    jal  print_str
    mv   a0, s10
    jal  print_hex
    la   a0, str_nl
    jal  print_str
    j    st_ret
st_noreply:
    la   a0, str_noreply
    jal  print_str
    j    st_ret

st_idle:
    jal  fifo_pop                   # a0 = cmd or -1
    bltz a0, st_ret
    mv   s7, a0
    li   s6, 3                      # 处理占用 3 个时间片 / takes 3 slices
    li   s8, 0
    li   t0, 2
    beq  s7, t0, st_setnoreply      # $02 eyecatcher: no reply
    li   t0, 3
    beq  s7, t0, st_setnoreply      # $03 reset: no reply
    j    st_print_proc
st_setnoreply:
    li   s8, 1
st_print_proc:
    la   a0, str_proc
    jal  print_str
    mv   a0, s7
    jal  print_hex
    la   a0, str_nl
    jal  print_str

st_ret:
    lw   ra, 0(sp)
    addi sp, sp, 4
    ret

# --- 4 项环形 FIFO / 4-entry ring FIFO ---------------------------------------
fifo_push:                          # a0 = value
    addi sp, sp, -4
    sw   ra, 0(sp)
    la   t0, fifo_count
    lw   t1, 0(t0)
    li   t2, 4
    blt  t1, t2, fp_store
    la   a0, str_full
    jal  print_str
    j    fp_ret
fp_store:
    addi t1, t1, 1
    sw   t1, 0(t0)
    la   t0, fifo_tail
    lw   t1, 0(t0)
    la   t2, fifo
    slli t3, t1, 2
    add  t2, t2, t3
    sw   a0, 0(t2)
    addi t1, t1, 1
    andi t1, t1, 3
    sw   t1, 0(t0)
fp_ret:
    lw   ra, 0(sp)
    addi sp, sp, 4
    ret

fifo_pop:                           # returns a0 = value, or -1 if empty
    la   t0, fifo_count
    lw   t1, 0(t0)
    bnez t1, fpo_take
    li   a0, -1
    ret
fpo_take:
    addi t1, t1, -1
    sw   t1, 0(t0)
    la   t0, fifo_head
    lw   t1, 0(t0)
    la   t2, fifo
    slli t3, t1, 2
    add  t2, t2, t3
    lw   a0, 0(t2)
    addi t1, t1, 1
    andi t1, t1, 3
    sw   t1, 0(t0)
    ret

# ===========================================================================
# LED 矩阵显示 / LED matrix display
#   LED (x, y) 的字节偏移 = (y * WIDTH + x) * 4，24-bit RGB，B 在最低字节
#   byte offset of LED (x, y) = (y * WIDTH + x) * 4, 24-bit RGB, B in the low byte
# ===========================================================================
draw_leds:
    addi sp, sp, -4
    sw   ra, 0(sp)

    li   a0, 1                      # row 1: command latch
    la   t0, cmd_latch
    lw   a1, 0(t0)
    li   a2, 0xFFD000
    jal  draw_byte

    li   a0, 3                      # row 3: reply latch
    la   t0, reply_latch
    lw   a1, 0(t0)
    li   a2, 0x00C8FF
    jal  draw_byte

    li   a0, 5                      # row 5, col 2: NMI pending (red)
    li   a1, 2
    la   t0, nmi_line
    lw   t1, 0(t0)
    li   a2, 0xFF2020
    beqz t1, dl_nmi_off
    j    dl_nmi_set
dl_nmi_off:
    li   a2, 0
dl_nmi_set:
    jal  set_led

    li   a0, 5                      # row 5, col 5: NMI enabled (green)
    li   a1, 5
    la   t0, nmi_enabled
    lw   t1, 0(t0)
    li   a2, 0x20FF40
    beqz t1, dl_en_off
    j    dl_en_set
dl_en_off:
    li   a2, 0
dl_en_set:
    jal  set_led

    li   a0, 5                      # row 5, col 8: Z80 busy (blue)
    li   a1, 8
    li   a2, 0x4060FF
    beqz s6, dl_busy_off
    j    dl_busy_set
dl_busy_off:
    li   a2, 0
dl_busy_set:
    jal  set_led

    li   a0, 5                      # row 5, col 11: 68000 waiting for ack (magenta)
    li   a1, 11
    li   a2, 0xFF40FF
    beqz s2, dl_wait_off
    j    dl_wait_set
dl_wait_off:
    li   a2, 0
dl_wait_set:
    jal  set_led

    # row 7, cols 2..5: FIFO occupancy (white)
    la   t0, fifo_count
    lw   s10, 0(t0)
    li   s11, 0
dl_fifo_loop:
    li   t0, 4
    bge  s11, t0, dl_ret
    li   a0, 7
    addi a1, s11, 2
    li   a2, 0xE0E0E0
    blt  s11, s10, dl_fifo_set
    li   a2, 0
dl_fifo_set:
    jal  set_led
    addi s11, s11, 1
    j    dl_fifo_loop

dl_ret:
    lw   ra, 0(sp)
    addi sp, sp, 4
    ret

# draw_byte: a0 = row, a1 = 8-bit value, a2 = colour; bit 7 at col 2 ... bit 0 at col 9
draw_byte:
    mv   t0, s0
    mv   t1, a0
db_rowloop:
    beqz t1, db_rowdone
    add  t0, t0, s1
    addi t1, t1, -1
    j    db_rowloop
db_rowdone:
    addi t0, t0, 8                  # col 2
    li   t1, 7
db_bitloop:
    srl  t2, a1, t1
    andi t2, t2, 1
    mv   t3, zero
    beqz t2, db_store
    mv   t3, a2
db_store:
    sw   t3, 0(t0)
    addi t0, t0, 4
    addi t1, t1, -1
    bgez t1, db_bitloop
    ret

# set_led: a0 = row, a1 = col, a2 = colour
set_led:
    mv   t0, s0
    mv   t1, a0
sl_rowloop:
    beqz t1, sl_rowdone
    add  t0, t0, s1
    addi t1, t1, -1
    j    sl_rowloop
sl_rowdone:
    slli t1, a1, 2
    add  t0, t0, t1
    sw   a2, 0(t0)
    ret

# ===========================================================================
# 工具 / helpers
# ===========================================================================
print_str:                          # a0 = string address
    li   a7, 4
    ecall
    ret

print_hex:                          # a0 = value
    li   a7, 34
    ecall
    ret

delay:                              # 让 LED 变化可见；单步调试时可把 t0 改小
    li   t0, 2000                   # makes LED changes visible; lower it when stepping
dl_loop:
    addi t0, t0, -1
    bnez t0, dl_loop
    ret

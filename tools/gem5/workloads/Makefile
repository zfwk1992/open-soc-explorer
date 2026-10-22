# Builds the gem5 workloads (static, freestanding RV64IM Linux ELFs).
RISCV_CC ?= riscv64-linux-gnu-gcc
CFLAGS = -march=rv64im -mabi=lp64 -static -nostdlib -nostartfiles -Wl,--build-id=none

all: loop_sum.elf array_sum.elf array_sum_seq.elf

%.elf: %.S
	$(RISCV_CC) $(CFLAGS) -o $@ $<

array_sum_seq.elf: array_sum.S
	$(RISCV_CC) $(CFLAGS) -DSTRIDE=1 -o $@ $<

clean:
	rm -f *.elf

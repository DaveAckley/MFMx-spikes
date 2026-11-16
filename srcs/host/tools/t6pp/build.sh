#!/bin/sh
# Build $B.bin (flat binary linked at T6 L1 address 0, BRISC reset address) with the sfpi gcc cross toolchain.
# Usage: sh t6pp/build.sh [t6pp]       Override the toolchain with SFPI=/path/to/sfpi/compiler.   Do not commit outputs.
set -e
cd "$(dirname "$0")"
B=${1:-t6pp}
T=${SFPI:-/opt/tenstorrent/sfpi/compiler}/bin/riscv32-tt-elf
$T-gcc -march=rv32im_zicsr -mabi=ilp32 -O2 -Wall -ffreestanding -nostdlib -fno-pic -mno-relax \
      -fno-asynchronous-unwind-tables -fno-stack-protector -c $B.c -o $B.o
$T-ld -T t6pp.ld --no-relax $B.o -o $B.elf
$T-objcopy -O binary $B.elf $B.bin
$T-objdump -d -M no-aliases $B.elf > $B.dis
# No .data/.bss: the linker script discards them and all state lives on the stack.
if $T-size -A $B.o | awk '$1 ~ /^\.(data|bss|sdata|sbss)/ && $2 > 0 {bad=1} END{exit !bad}'; then
  echo "ERROR: $B.o has .data/.bss content (T6 pp images keep all state on the stack)" >&2
  exit 1
fi
$T-size $B.elf
ls -l $B.bin

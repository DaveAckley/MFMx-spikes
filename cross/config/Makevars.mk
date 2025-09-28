## CROSS TOOLS
COMPILER_BASE_DIR:=/opt/tenstorrent/sfpi/compiler
CC:=$(COMPILER_BASE_DIR)/bin/riscv32-tt-elf-gcc
CFLAGS:=-Os
CXX:=$(COMPILER_BASE_DIR)/bin/riscv32-tt-elf-g++
#CXX:=$(COMPILER_BASE_DIR)/bin/riscv32-tt-elf-sfpi
CXXFLAGS:=-Os
LD:=$(COMPILER_BASE_DIR)/bin/riscv32-tt-elf-ld
AR:=$(COMPILER_BASE_DIR)/bin/riscv32-tt-elf-ar
SIM:=$(COMPILER_BASE_DIR)/bin/riscv32-tt-elf-run
SIZE:=$(COMPILER_BASE_DIR)/bin/riscv32-tt-elf-size
OBJCOPY:=$(COMPILER_BASE_DIR)/bin/riscv32-tt-elf-objcopy
OBJDUMP:=$(COMPILER_BASE_DIR)/bin/riscv32-tt-elf-objdump

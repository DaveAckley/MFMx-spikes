# This is a generic example. You need to adjust it for your specific cross-compiler.
# For ARM GCC, for instance:

set(CMAKE_SYSTEM_NAME Generic) # Or Linux, Windows, etc., depending on your target OS
set(CMAKE_SYSTEM_PROCESSOR arm) # Or whatever your target architecture is

# Specify the cross-compiler executables
set(COMPILER_BASE_DIR  /opt/tenstorrent/sfpi/compiler)
set(CMAKE_C_COMPILER   ${COMPILER_BASE_DIR}/bin/riscv32-tt-elf-gcc)
set(CMAKE_CXX_COMPILER ${COMPILER_BASE_DIR}/bin/riscv32-tt-elf-g++)
set(CMAKE_OBJCOPY ${COMPILER_BASE_DIR}/bin/riscv32-tt-elf-objcopy)
set(CMAKE_OBJDUMP ${COMPILER_BASE_DIR}/bin/riscv32-tt-elf-objdump)
set(CMAKE_SIZE ${COMPILER_BASE_DIR}/bin/riscv32-tt-elf-size)

#set(CMAKE_ASM_COMPILER ${COMPILER_BASE_DIR}/bin/riscv32-tt-elf-as)
#message(STATUS "ZONGSR")
#set(CMAKE_ASM_COMPILER ${COMPILER_BASE_DIR}/bin/riscv32-tt-elf-gcc) # use gcc to get .S preprocessing?

# Specify the target environment root (sysroot) if needed
# set(CMAKE_FIND_ROOT_PATH /path/to/your/cross/compiler/sysroot)

# Only search in the specified root path
# set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
# set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
# set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)

# Add any specific compiler flags for the cross-compiler
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mcpu=cortex-m4 -mthumb")
set(MARCH rv32ima_zicsr_zba_zbb)
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=${MARCH} -ffreestanding -nostdlib -fno-exceptions -fno-rtti")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=${MARCH} -ffreestanding -nostdlib -fno-exceptions -fno-rtti -save-temps")
# Thu Jan  1 22:25:47 2026 -fmacro-prefix-map: Shorten the paths reported by __FILE__ (e.g. in FAILs)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=${MARCH} -ffreestanding -nostdlib -fno-exceptions -fno-rtti -save-temps -fmacro-prefix-map=${CMAKE_SOURCE_DIR}=")
set(CMAKE_ASM_FLAGS "-march=${MARCH}")

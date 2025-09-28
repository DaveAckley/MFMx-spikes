#ifndef CONSTANTS_H
#define CONSTANTS_H

/*
 * SPDX-FileCopyrightText: © 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(MAP_ANON) && !defined(MAP_ANONYMOUS)
#define MAP_ANONYMOUS MAP_ANON
#endif

// Inlined copy of what we need from https://github.com/tenstorrent/tt-kmd/blob/main/ioctl.h:

#define TENSTORRENT_IOCTL_GET_DEVICE_INFO   0xFA00
#define TENSTORRENT_IOCTL_QUERY_MAPPINGS    0xFA02
#define TENSTORRENT_IOCTL_ALLOCATE_TLB      0xFA0B

#define TENSTORRENT_MAPPING_RESOURCE0_UC 1

#define PCI_VENDOR_ID_TENSTORRENT 0x1E52
#define PCI_DEVICE_ID_BLACKHOLE	  0xB140

#define TLB_CONFIG_ADDR         0x1FC00000
#define TLB_CONFIG_ADDR_STRIDES 0x1FC009D8
#define TLB_CONFIG_ADDR_END     0x1FC00A58

// RISCV Code entry points:

#define RISCV_DEBUG_REG_SOFT_RESET_0             0xFFB121B0
#define RISCV_DEBUG_REG_TRISC0_RESET_PC          0xFFB12228
#define RISCV_DEBUG_REG_TRISC1_RESET_PC          0xFFB1222C
#define RISCV_DEBUG_REG_TRISC2_RESET_PC          0xFFB12230
#define RISCV_DEBUG_REG_TRISC_RESET_PC_OVERRIDE  0xFFB12234
#define RISCV_DEBUG_REG_NCRISC_RESET_PC          0xFFB12238
#define RISCV_DEBUG_REG_NCRISC_RESET_PC_OVERRIDE 0xFFB1223C

#define SOFT_RESET_ALL_RISCV 0x047800

#define RISCV_DEBUG_B_PC_SNAPSHOT 0xFFB13138
#define RISCV_DEBUG_NC_PC_SNAPSHOT 0xFFB1313C
#define RISCV_DEBUG_T0_PC_SNAPSHOT 0xFFB13140
#define RISCV_DEBUG_T1_PC_SNAPSHOT 0xFFB13144
#define RISCV_DEBUG_T2_PC_SNAPSHOT 0xFFB13148

#endif /* CONSTANTS_H */

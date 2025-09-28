#ifndef CONSTANTS_H
#define CONSTANTS_H

// Inlined copy of what we need from https://github.com/tenstorrent/tt-kmd/blob/main/ioctl.h:

#define TENSTORRENT_IOCTL_GET_DEVICE_INFO   0xFA00
#define TENSTORRENT_IOCTL_QUERY_MAPPINGS    0xFA02
#define TENSTORRENT_IOCTL_ALLOCATE_DMA_BUF  0xFA03
#define TENSTORRENT_IOCTL_PIN_PAGES         0xFA07
#define TENSTORRENT_IOCTL_ALLOCATE_TLB      0xFA0B
#define TENSTORRENT_IOCTL_SET_NOC_CLEANUP   0xFA0E

#define TENSTORRENT_MAPPING_RESOURCE0_UC 1

#define TENSTORRENT_ALLOCATE_DMA_BUF_NOC_DMA 2

#define TENSTORRENT_PIN_PAGES_NOC_DMA      2 // app wants to use the pages for NOC DMA
#define TENSTORRENT_PIN_PAGES_NOC_TOP_DOWN 4

#define PCI_VENDOR_ID_TENSTORRENT 0x1E52
#define PCI_DEVICE_ID_BLACKHOLE	  0xB140

#define TLB_CONFIG_ADDR         0x1FC00000
#define TLB_CONFIG_ADDR_STRIDES 0x1FC009D8
#define TLB_CONFIG_ADDR_END     0x1FC00A58

// Definitions for Ethernet tile address space:

#define ETH_BOOT_PARAMS_ADDR                    0x0007C000
#define ETH_BOOT_RESULTS_ADDR                   0x0007CC00
#define SOFT_RESET_ADDR                         0xFFB121B0
#define E1_RESET_PC_ADDR                        0xFFB14008
#define E1_END_PC_ADDR                          0xFFB1400C
#define NIU_ADDR(i)                            (0xFFB20000 + (i)*0x10000)
#define TXQ_ADDR(i)                            (0xFFB90000 + (i)*0x1000)
#define RXQ_ADDR(i)                            (0xFFB94000 + (i)*0x1000)
#define TXPKT_CFG_ADDR(i)                      (0xFFB98200 + (i)*0x80)
#define RXCLASS_MAC_RX_ROUTING_ADDR             0xFFB98150
#define RXCLASS_USER_DEFINED_ETHERTYPE_ADDR(i) (0xFFB9C000 + (i)*4)
#define RXCLASS_NO_MATCH_ACTIONS_ADDR           0xFFB9CD04
#define RXCLASS_TCAM_FLUSH                      0xFFB9CD60
#define RXCLASS_OVERRIDE_DECISION_ADDR          0xFFB9D000

// Offsets from NIU_ADDR:
#define NOC_TARG_ADDR_LO_OFFSET     0x000
#define NOC_TARG_ADDR_MID_OFFSET    0x004
#define NOC_TARG_ADDR_HI_OFFSET     0x008
#define NOC_RET_ADDR_LO_OFFSET      0x00C
#define NOC_RET_ADDR_MID_OFFSET     0x010
#define NOC_RET_ADDR_HI_OFFSET      0x014
#define NOC_PACKET_TAG_OFFSET       0x018
#define NOC_CTRL_OFFSET             0x01C
#define NOC_AT_LEN_BE_OFFSET        0x020
#define NOC_AT_LEN_BE_1_OFFSET      0x024
#define NOC_BRCST_EXCLUDE_OFFSET    0x02C
#define NOC_L1_ACC_AT_INSTRN_OFFSET 0x030
#define NOC_ENDPOINT_ID_OFFSET      0x048
#define NIU_CFG_0_OFFSET            0x100
#define ROUTER_CFG_2_OFFSET         0x10C // Has no hardware-defined meaning; we repurpose it for a host-to-device mailbox.
#define ROUTER_CFG_4_OFFSET         0x114 // Has no hardware-defined meaning; we repurpose it for host informing device of its read pointer.
#define NOC_ID_LOGICAL_OFFSET       0x148

// Offsets from TXQ_ADDR:
#define ETH_TXQ_CTRL_OFFSET                0x00
#define ETH_TXQ_CMD_OFFSET                 0x04
#define ETH_TXQ_TRANSFER_START_ADDR_OFFSET 0x14
#define ETH_TXQ_TRANSFER_SIZE_BYTES_OFFSET 0x18
#define ETH_TXQ_REMOTE_SEQ_TIMEOUT_OFFSET  0x48
#define ETH_TXQ_TXPKT_CFG_SEL_SW_OFFSET    0x80

// Offsets from RXQ_ADDR:
#define ETH_RXQ_CTRL_OFFSET                0x00
#define ETH_RXQ_BUF_PTR_OFFSET             0x08
#define ETH_RXQ_BUF_START_WORD_ADDR_OFFSET 0x0C
#define ETH_RXQ_BUF_SIZE_WORDS_OFFSET      0x10
#define ETH_RXQ_HDR_CTRL_OFFSET            0x18
#define ETH_RXQ_PACKET_DROP_CNT_OFFSET     0x4C

// Offsets from TXPKT_CFG_ADDR:
#define TXPKT_CFG_INSERT_CTL_OFFSET    0x00
#define TXPKT_CFG_MAC_SA_OFFSET        0x10
#define TXPKT_CFG_USE_ETHERTYPE_OFFSET 0x20
#define TXPKT_CFG_L3_HEADER_OFFSET     0x30
#define TXPKT_CFG_L4_HEADER_OFFSET     0x60

// Values for TXPKT_CFG_INSERT_CTL_OFFSET:
#define TXPKT_CFG_INSERT_CTL_L3_HEADER   (1u <<  8)
#define TXPKT_CFG_INSERT_CTL_L4_HEADER   (1u << 16)
#define TXPKT_CFG_INSERT_CTL_L4_CHECKSUM (1u << 18)

// Values for RXCLASS_MAC_RX_ROUTING_ADDR:
#define RXCLASS_MAC_RX_ROUTING_FROM_MAC     0
#define RXCLASS_MAC_RX_ROUTING_FROM_ACTIONS 2

// Values for RXCLASS_NO_MATCH_ACTIONS_ADDR:
#define RXCLASS_NO_MATCH_ACTIONS_TO_RXQ(i)            (i)
#define RXCLASS_NO_MATCH_ACTIONS_DROP                   4
#define RXCLASS_NO_MATCH_ACTIONS_PREPEND_HW_METADATA 0x40

// Values for RXCLASS_OVERRIDE_DECISION_ADDR:
#define RXCLASS_OVERRIDE_DECISION_ACCEPT  0
#define RXCLASS_OVERRIDE_DECISION_DROP    1
#define RXCLASS_OVERRIDE_DECISION_REGULAR 2

// Values for NOC_RET_ADDR_HI_OFFSET:
#define BH_PCIE_XY (19 + (24 << 6))

// Values for NOC_CTRL_OFFSET:
#define NOC_CMD_WR 2
#define NOC_CMD_VC_STATIC (1u << 7)

// Values for NIU_CFG_0_OFFSET:
#define NIU_CFG_0_HARVESTED (1u << 12)

// Values for SOFT_RESET_ADDR:
#define SOFT_RESET_E0 0x0800
#define SOFT_RESET_E1 0x1000

#define PCAP_WRITER_NUM_IOVS 20

#endif /* CONSTANTS_H */

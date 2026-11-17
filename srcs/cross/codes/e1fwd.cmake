include_guard()
# E1 router images for the Ethernet tiles (eth-seam; host/tools/ERISC_SEAM_DESIGN.md).
#
# Not T6 code: bare-metal rv32im_zicsr for the subordinate ERISC, linked at
# ETH L1 0x4000 by host/tools/e1fwd/e1fwd.ld.  The same sfpi toolchain builds
# them; host/tools/e1fwd/build.sh does the work (including its refusal of
# .data/.bss).  Each image lands in ${CMAKE_BINARY_DIR}/bin/<name>.bin, next to
# hub.bin and ewp.bin, which is where mfmxctl's bindirs look.
#
# To add an image: put <name>.c in host/tools/e1fwd and list <name> here.
set(E1FWD_SRC_DIR "${CMAKE_SOURCE_DIR}/srcs/host/tools/e1fwd")
set(E1FWD_IMAGES e1fwd16)

if(NOT DEFINED COMPILER_BASE_DIR)
  set(COMPILER_BASE_DIR /opt/tenstorrent/sfpi/compiler)
endif()

set(E1FWD_BINS "")
foreach(img ${E1FWD_IMAGES})
  set(E1_WORK "${CMAKE_CURRENT_BINARY_DIR}/e1fwd")
  set(E1_BIN "${CMAKE_BINARY_DIR}/bin/${img}.bin")
  add_custom_command(
    OUTPUT ${E1_BIN}
    COMMAND ${CMAKE_COMMAND} -E env
      SFPI=${COMPILER_BASE_DIR}
      OUTDIR=${E1_WORK}
      QUIET=1
      sh ${E1FWD_SRC_DIR}/build.sh ${img}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin
    COMMAND ${CMAKE_COMMAND} -E copy ${E1_WORK}/${img}.bin ${E1_BIN}
    DEPENDS
      ${E1FWD_SRC_DIR}/${img}.c
      ${E1FWD_SRC_DIR}/e1fwd.ld
      ${E1FWD_SRC_DIR}/build.sh
    COMMENT "Building E1 image ${img} -> ${E1_BIN}"
    VERBATIM)
  list(APPEND E1FWD_BINS ${E1_BIN})
endforeach()

add_custom_target(e1fwd ALL DEPENDS ${E1FWD_BINS})

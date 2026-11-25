include_guard()
message("ZOTL ${CMAKE_CURRENT_LIST_FILE}")
message("ZOTS ${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(zot)
target_sources(zot PRIVATE
  images/zot/src/LiveB.cpp
  images/zot/src/ZotBlock.cpp
  images/zot/src/ZotBoltResponder.cpp
  images/zot/src/demo.cpp
  src/ImageConfig.cpp
  ${S_FILES_LIST}
  ${SHARED_SOURCES})

# Assign global file IDs to crosslib files
#assign_file_ids(zot)
  
target_link_libraries(zot PRIVATE crosslib)
target_include_directories(zot PRIVATE
  images/zot/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${SHARED_INCLUDE_DIRS})
target_compile_options(zot PRIVATE ${CROSS_COMPILER_OPTIONS})

# --- Link with the GENERATED custom linker script ---
target_link_options(zot PRIVATE
  -T${GENERATED_LDSCRIPT}
  -nostartfiles
  -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/zot.map
)

# --- Linker Libraries (Order matters for these!) ---
# These will be placed at the end of the linker command line.
target_link_libraries(zot PRIVATE
  -lgcc
  # Add any other libraries that need to be linked after your object files
)

set(ZOT_BINFILE_PATH "${CMAKE_BINARY_DIR}/bin/zot.bin")
set(ZOT_DISASM_PATH "${CMAKE_CURRENT_BINARY_DIR}/zot.s")

# Add a custom command that runs AFTER zot is linked
add_custom_command(
  TARGET zot POST_BUILD # This command runs after zot is built
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:zot> | tail -1 | # Extract last line of T6 code size 
  ${PERL_SIZER_PATH} - $<TARGET_FILE:zot> | # format with time info
  tee -a ${CMAKE_SOURCE_DIR}/../CROSS_BUILD_HISTORICAL/cross-builds.dat # and preserve in amber
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:zot> ${ZOT_BINFILE_PATH}
  COMMAND ${CMAKE_OBJDUMP} -C -D $<TARGET_FILE:zot> > ${ZOT_DISASM_PATH}  # Disassemble for debug
  COMMENT "Generating ${ZOT_BINFILE_PATH} from zot ($<TARGET_FILE:zot>)"
)


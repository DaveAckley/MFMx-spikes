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

set(ZOT_EXPORTS_HEADER "${CMAKE_CURRENT_BINARY_DIR}/t6-exports.h")
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
  COMMAND ${CMAKE_OBJDUMP} -C -t $<TARGET_FILE:zot> | # Dump symbol table
  sort -r -k 5 - | # alphabetize entries for OCD
  perl -n ${PERL_FORMATTER_PATH} - > ${ZOT_EXPORTS_HEADER}
  COMMENT "Generating ${ZOT_BINFILE_PATH} and ${ZOT_EXPORTS_HEADER} from zot ($<TARGET_FILE:zot>)"
)

# --- Copy the generated header to a shared location ---
set(SHARED_GENERATED_HEADERS_DIR "${CMAKE_SOURCE_DIR}/srcs/generated_headers")
add_custom_command(
  TARGET zot POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SHARED_GENERATED_HEADERS_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy ${ZOT_EXPORTS_HEADER} ${SHARED_GENERATED_HEADERS_DIR}/t6-exports.h
  COMMENT "Copying ${ZOT_EXPORTS_HEADER} to shared location"
)


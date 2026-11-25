include_guard()
message("EWPL ${CMAKE_CURRENT_LIST_FILE}")
message("EWPS ${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(ewp)
target_sources(ewp PRIVATE
  allimg/src/HartTasks.cpp
  allimg/src/ImageConfig.cpp
  images/ewp/src/EwpBlock.cpp
  images/ewp/src/EwpBoltResponder.cpp
  images/ewp/src/LiveB.cpp
  images/ewp/src/PhaseSteppers.cpp
  images/ewp/src/Physics.cpp
  ${S_FILES_LIST}
  ${SHARED_SOURCES})

# Assign global file IDs to crosslib files
#assign_file_ids(ewp)
  
target_link_libraries(ewp PRIVATE crosslib)
target_include_directories(ewp PRIVATE
  allimg/include
  images/ewp/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${SHARED_INCLUDE_DIRS})
target_compile_options(ewp PRIVATE ${CROSS_COMPILER_OPTIONS})

# --- Link with the GENERATED custom linker script ---
target_link_options(ewp PRIVATE
  -T${GENERATED_LDSCRIPT}
  -nostartfiles
  -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/ewp.map
)

# --- Linker Libraries (Order matters for these!) ---
# These will be placed at the end of the linker command line.
target_link_libraries(ewp PRIVATE
  -lgcc
  # Add any other libraries that need to be linked after your object files
)

set(EWP_BINFILE_PATH "${CMAKE_BINARY_DIR}/bin/ewp.bin")
set(EWP_DISASM_PATH "${CMAKE_CURRENT_BINARY_DIR}/ewp.s")

# Add a custom command that runs AFTER ewp is linked
add_custom_command(
  TARGET ewp POST_BUILD # This command runs after ewp is built
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:ewp> | tail -1 | # Extract last line of T6 code size 
  ${PERL_SIZER_PATH} - $<TARGET_FILE:ewp> | # format with time info
  tee -a ${CMAKE_SOURCE_DIR}/../CROSS_BUILD_HISTORICAL/cross-builds.dat # and preserve in amber
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:ewp> ${EWP_BINFILE_PATH}
  COMMAND ${CMAKE_OBJDUMP} -C -D $<TARGET_FILE:ewp> > ${EWP_DISASM_PATH}  # Disassemble for debug
  COMMENT "Generating ${EWP_BINFILE_PATH} from ewp ($<TARGET_FILE:ewp>)"
)


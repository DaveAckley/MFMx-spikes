include_guard()
message("HUBL ${CMAKE_CURRENT_LIST_FILE}")
message("HUBS ${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(hub)
target_sources(hub PRIVATE
  allimg/src/HartTasks.cpp
  allimg/src/ImageConfig.cpp
  src/ArcTelemetry.cpp
  images/hub/src/CacheLoader.cpp
  images/hub/src/EP_ACacheBlock.cpp
  images/hub/src/EP_InterHub.cpp
  images/hub/src/EwpBlock.cpp
  images/hub/src/Grid.cpp
  images/hub/src/HubCacheMgr.cpp
  images/hub/src/InterHub.cpp
  images/hub/src/LiveB.cpp
  images/hub/src/LiveT1.cpp
  images/hub/src/PhaseSteppers.cpp
#  images/hub/src/T1fastlz.cpp
  ${S_FILES_LIST}
  ${SHARED_SOURCES})

target_link_libraries(hub PRIVATE crosslib)
target_include_directories(hub PRIVATE
  allimg/include
  images/hub/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${SHARED_INCLUDE_DIRS})
target_compile_options(hub PRIVATE ${CROSS_COMPILER_OPTIONS})

# --- Link with the GENERATED custom linker script ---
target_link_options(hub PRIVATE
  -T${GENERATED_LDSCRIPT}
  -nostartfiles
  -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/hub.map
)

# --- Linker Libraries (Order matters for these!) ---
# These will be placed at the end of the linker command line.
target_link_libraries(hub PRIVATE
  -lgcc
  # Add any other libraries that need to be linked after your object files
)

set(HUB_BINFILE_PATH "${CMAKE_BINARY_DIR}/bin/hub.bin")
set(HUB_DISASM_PATH "${CMAKE_CURRENT_BINARY_DIR}/hub.s")

# Add a custom command that runs AFTER hub is linked
add_custom_command(
  TARGET hub POST_BUILD # This command runs after hub is built
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:hub> | tail -1 | # Extract last line of T6 code size 
  ${PERL_SIZER_PATH} - $<TARGET_FILE:hub> | # format with time info
  tee -a ${CMAKE_SOURCE_DIR}/../CROSS_BUILD_HISTORICAL/cross-builds.dat # and preserve in amber
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:hub> ${HUB_BINFILE_PATH}
  COMMAND ${CMAKE_OBJDUMP} -C -D $<TARGET_FILE:hub> > ${HUB_DISASM_PATH}  # Disassemble for debug
  COMMENT "Generating ${HUB_BINFILE_PATH} from hub ($<TARGET_FILE:hub>)"
)


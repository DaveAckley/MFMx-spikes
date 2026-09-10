include_guard()
message("HUBL ${CMAKE_CURRENT_LIST_FILE}")
message("HUBS ${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(hub)
target_sources(hub PRIVATE
  allimg/src/HartTasks.cpp
  allimg/src/ImageConfig.cpp
  images/hub/src/EP_ACBlock.cpp
  images/hub/src/EP_ACacheBlock.cpp
  images/hub/src/EP_InterHub.cpp
  images/hub/src/EwpBlock.cpp
  images/hub/src/HubTaskManager.cpp
  images/hub/src/Grid.cpp
  images/hub/src/InterHub.cpp
  images/hub/src/LiveB.cpp
  images/hub/src/LiveT1.cpp
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

set(HUB_EXPORTS_HEADER "${CMAKE_CURRENT_BINARY_DIR}/t6-exports.h")
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
  COMMAND ${CMAKE_OBJDUMP} -C -t $<TARGET_FILE:hub> | # Dump symbol table
  sort -r -k 5 - | # alphabetize entries for OCD
  perl -n ${PERL_FORMATTER_PATH} - > ${HUB_EXPORTS_HEADER}
  COMMENT "Generating ${HUB_BINFILE_PATH} and ${HUB_EXPORTS_HEADER} from hub ($<TARGET_FILE:hub>)"
)

# --- Copy the generated header to a shared location ---
set(SHARED_GENERATED_HEADERS_DIR "${CMAKE_SOURCE_DIR}/srcs/generated_headers")
add_custom_command(
  TARGET hub POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SHARED_GENERATED_HEADERS_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy ${HUB_EXPORTS_HEADER} ${SHARED_GENERATED_HEADERS_DIR}/t6-exports.h
  COMMENT "Copying ${HUB_EXPORTS_HEADER} to shared location"
)


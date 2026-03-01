include_guard()
message("EWPL ${CMAKE_CURRENT_LIST_FILE}")
message("EWPS ${CMAKE_CURRENT_SOURCE_DIR}")
add_executable(ewp)
target_sources(ewp PRIVATE
  src/ImageConfig.cpp
  images/ewp/src/LiveB.cpp
  images/ewp/src/E2HEP.cpp
  ${S_FILES_LIST}
  ${SHARED_SOURCES})
target_link_libraries(ewp PRIVATE crosslib)
target_include_directories(ewp PRIVATE
  images/ewp/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${SHARED_INCLUDE_DIRS})
target_compile_options(ewp PRIVATE ${CROSS_COMPILER_OPTIONS})
# --- Link with the GENERATED custom linker script ---
target_link_options(ewp PRIVATE
  -T${GENERATED_LDSCRIPT}
)

# --- Linker Libraries (Order matters for these!) ---
# These will be placed at the end of the linker command line.
target_link_libraries(ewp PRIVATE
  -lgcc
  # Add any other libraries that need to be linked after your object files
)

# Ensure the executable depends on the generated linker script target
add_dependencies(ewp ${GENERATED_LDSCRIPT_TARGET_NAME}) 

# --- Generate binary and t6-exports.h after linking ---
# Define the output header file path
set(EWP_EXPORTS_HEADER "${CMAKE_CURRENT_BINARY_DIR}/t6-exports.h")
set(EWP_BINFILE_PATH "${CMAKE_BINARY_DIR}/bin/ewp.bin")

# Add a custom command that runs AFTER ewp is linked
add_custom_command(
  TARGET ewp POST_BUILD # This command runs after ewp is built
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:ewp> | tail -1 | # Extract last line of T6 code size 
  ${PERL_SIZER_PATH} - $<TARGET_FILE:ewp> | # format with time info
  tee -a ${CMAKE_SOURCE_DIR}/../CROSS_BUILD_HISTORICAL/cross-builds.dat # and preserve in amber
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:ewp> ${EWP_BINFILE_PATH}
  COMMAND ${CMAKE_OBJDUMP} -C -t $<TARGET_FILE:ewp> | # Dump symbol table
  sort -r -k 5 - | # alphabetize entries for OCD
  perl -n ${PERL_FORMATTER_PATH} - > ${EWP_EXPORTS_HEADER}
  COMMENT "Generating ${EWP_BINFILE_PATH} and ${EWP_EXPORTS_HEADER} from ewp ($<TARGET_FILE:ewp>)"
)

add_custom_command(
  TARGET ewp POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SHARED_GENERATED_HEADERS_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy ${EWP_EXPORTS_HEADER} ${SHARED_GENERATED_HEADERS_DIR}/t6-exports.h
  COMMENT "Copying ${EWP_EXPORTS_HEADER} to shared location"
)

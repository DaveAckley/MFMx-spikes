# Cross-compiled specific build
set(S_FILES_LIST
  src/_BUD.S
  src/memops.S
)

add_executable(crossmain
  src/AtomicLock.cpp
  src/CrossUtils.cpp
  src/FATAL.cpp
  src/FastB.cpp
  src/FastLocal.cpp
  src/FastNC.cpp
  src/FastT0.cpp
  src/FastT1.cpp
  src/FastT2.cpp
#  src/NoCs.cpp
  src/P2PLogElevatorPlatform.cpp
  src/P2PEWElevatorPlatform.cpp
  src/Printf.cpp
  src/T6ElevatorTransport.cpp
  src/t6main.cpp
  src/T6RingO.cpp
  src/U8C.cpp
  src/Wrap8.cpp
  ${S_FILES_LIST}
  ${SHARED_SOURCES}
  src/formatSymbols.pl
)

target_include_directories(crossmain PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${SHARED_INCLUDE_DIRS}
)

# --- Define the path to the generated linker script ---
set(GENERATED_LDSCRIPT "${CMAKE_CURRENT_BINARY_DIR}/gen.ld")
# Define a short, unique name for the custom target that generates the linker script
set(GENERATED_LDSCRIPT_TARGET_NAME "gen_ld") 

# --- Create a custom target for generating the linker script ---
add_custom_target(${GENERATED_LDSCRIPT_TARGET_NAME}
  COMMAND ${CMAKE_CXX_COMPILER} # Use the C++ compiler for preprocessing
  -E                     # Preprocess only
  -P                     # Disable line markers
  -x c                   # Treat input as C source
  -I${CMAKE_CURRENT_SOURCE_DIR}/include # Add include path for dev_mem_map.h
  -I${SHARED_INCLUDE_DIRS} # Add shared include paths if needed by ldscript.ld.in
  ${CMAKE_CURRENT_SOURCE_DIR}/src/_BUD.ld.in # Input template file
  -o ${GENERATED_LDSCRIPT} # Output file
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/_BUD.ld.in
  ${CMAKE_CURRENT_SOURCE_DIR}/include/dev_mem_map.h # Dependency on the header file
  COMMENT "Preprocessing linker script ${GENERATED_LDSCRIPT}"
  BYPRODUCTS ${GENERATED_LDSCRIPT} # Tell CMake this target produces this file
)

# --- Link with the GENERATED custom linker script ---
target_link_options(crossmain PRIVATE
  -T${GENERATED_LDSCRIPT}
)

# --- Linker Libraries (Order matters for these!) ---
# These will be placed at the end of the linker command line.
target_link_libraries(crossmain PRIVATE
  -lgcc
  # Add any other libraries that need to be linked after your object files
)

# our hackogack objdump parser
set(PERL_FORMATTER_PATH "${CMAKE_SOURCE_DIR}/srcs/cross/src/formatSymbols.pl")
# and our hackogack size parser
set(PERL_SIZER_PATH "${CMAKE_SOURCE_DIR}/srcs/cross/src/formatSize.pl")

# Ensure the executable depends on the generated linker script target
add_dependencies(crossmain ${GENERATED_LDSCRIPT_TARGET_NAME}) 

# --- Generate binary and t6-exports.h after linking ---
# Define the output header file path
set(CROSSMAIN_EXPORTS_HEADER "${CMAKE_CURRENT_BINARY_DIR}/t6-exports.h")
set(CROSSMAIN_BINFILE_PATH "${CMAKE_BINARY_DIR}/bin/crossmain.bin")

# Add a custom command that runs AFTER crossmain is linked
add_custom_command(
  TARGET crossmain POST_BUILD # This command runs after crossmain is built
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:crossmain> | tail -1 | # Extract last line of T6 code size 
  ${PERL_SIZER_PATH} - $<TARGET_FILE:crossmain> | # format with time info
  tee -a ${CMAKE_SOURCE_DIR}/../CROSS_BUILD_HISTORICAL/cross-builds.dat # and preserve in amber
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:crossmain> ${CROSSMAIN_BINFILE_PATH}
  COMMAND ${CMAKE_OBJDUMP} -C -t $<TARGET_FILE:crossmain> | # Dump symbol table
  sort -r -k 5 - | # alphabetize entries for OCD
  perl -n ${PERL_FORMATTER_PATH} - > ${CROSSMAIN_EXPORTS_HEADER}
  COMMENT "Generating ${CROSSMAIN_BINFILE_PATH} and ${CROSSMAIN_EXPORTS_HEADER} from crossmain ($<TARGET_FILE:crossmain>)"
)

# --- Copy the generated header to a shared location ---
set(SHARED_GENERATED_HEADERS_DIR "${CMAKE_SOURCE_DIR}/srcs/generated_headers")
add_custom_command(
  TARGET crossmain POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SHARED_GENERATED_HEADERS_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy ${CROSSMAIN_EXPORTS_HEADER} ${SHARED_GENERATED_HEADERS_DIR}/t6-exports.h
  COMMENT "Copying ${CROSSMAIN_EXPORTS_HEADER} to shared location"
)

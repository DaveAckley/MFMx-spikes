# Cross-compiled specific build
set(S_FILES_LIST
  src/_BUD.S
  src/memops.S
)

# Add C++ compiler options specifically for cross ---
set(CROSS_COMPILER_OPTIONS
  # -g          # Add debug symbols
  -Os         # be small
  -Wall       # Enable all common warnings
  -Wextra     # Enable extra warnings
  -Wno-unused-parameter  # stuff we want
  -Wno-unused-variable   # to live with
  -Wno-unused-function   # let ld sort it out
  -Wno-missing-field-initializers   # too many to be worth it?
  -Werror                # die on warnings for now
  # Add any other host-specific C++ flags here
)

add_library(crosslib STATIC)
  
target_sources(crosslib PUBLIC
  src/AtomicLock.cpp
  src/CornerState.cpp
  src/CrossUtils.cpp
  src/FATAL.cpp
  src/FastB.cpp
  src/FastLocal.cpp
  src/FastNC.cpp
  src/FastT0.cpp
  src/FastT1.cpp
  src/FastT2.cpp
  src/P2PEWElevatorPlatform.cpp
  src/P2PLogElevatorPlatform.cpp
  src/Printf.cpp
  src/T6ElevatorTransport.cpp
  src/T6Grid.cpp
  src/T6RingO.cpp
  src/U8C.cpp
  src/Wrap8.cpp
  src/formatSymbols.pl
  src/t6main.cpp
)

target_include_directories(crosslib PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${SHARED_INCLUDE_DIRS}
)

# set(CROSS_EXECUTABLES
#   ewp
#   hub
# )

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

add_executable(ewp)
target_sources(ewp PRIVATE
  src/ImageConfig.cpp
  images/ewp/src/LiveB.cpp
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


add_executable(hub)
target_sources(hub PRIVATE
  src/ImageConfig.cpp
  images/hub/src/LiveB.cpp
  ${S_FILES_LIST}
  ${SHARED_SOURCES})
target_link_libraries(hub PRIVATE crosslib)
target_include_directories(hub PRIVATE
  images/hub/include
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${SHARED_INCLUDE_DIRS})
target_compile_options(hub PRIVATE ${CROSS_COMPILER_OPTIONS})
# --- Link with the GENERATED custom linker script ---
target_link_options(hub PRIVATE
  -T${GENERATED_LDSCRIPT} -Wl,-Map ${GENERATED_LDSCRIPT}.map
)

# --- Linker Libraries (Order matters for these!) ---
# These will be placed at the end of the linker command line.
target_link_libraries(hub PRIVATE
  -lgcc
  # Add any other libraries that need to be linked after your object files
)

# our hackogack objdump parser
set(PERL_FORMATTER_PATH "${CMAKE_SOURCE_DIR}/srcs/cross/src/formatSymbols.pl")
# and our hackogack size parser
set(PERL_SIZER_PATH "${CMAKE_SOURCE_DIR}/srcs/cross/src/formatSize.pl")

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

# --- Copy the generated header to a shared location ---
set(SHARED_GENERATED_HEADERS_DIR "${CMAKE_SOURCE_DIR}/srcs/generated_headers")
add_custom_command(
  TARGET ewp POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory ${SHARED_GENERATED_HEADERS_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy ${EWP_EXPORTS_HEADER} ${SHARED_GENERATED_HEADERS_DIR}/t6-exports.h
  COMMENT "Copying ${EWP_EXPORTS_HEADER} to shared location"
)

set(HUB_EXPORTS_HEADER "${CMAKE_CURRENT_BINARY_DIR}/t6-exports.h")
set(HUB_BINFILE_PATH "${CMAKE_BINARY_DIR}/bin/hub.bin")

# Add a custom command that runs AFTER hub is linked
add_custom_command(
  TARGET hub POST_BUILD # This command runs after hub is built
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:hub> | tail -1 | # Extract last line of T6 code size 
  ${PERL_SIZER_PATH} - $<TARGET_FILE:hub> | # format with time info
  tee -a ${CMAKE_SOURCE_DIR}/../CROSS_BUILD_HISTORICAL/cross-builds.dat # and preserve in amber
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:hub> ${HUB_BINFILE_PATH}
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

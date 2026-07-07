# srcs/cross/CMakeLists.txt
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
  -Wno-array-bounds      # just too many false positives on this?
  # Add any other host-specific C++ flags here
)

add_library(crosslib STATIC)

target_sources(crosslib PUBLIC
  src/CrossUtils.cpp
  src/DefaultLives.cpp
  src/EP_Ewp.cpp
  src/EP_InterHub.cpp
  src/EP_LogBlock.cpp
  src/FATAL.cpp
  src/FastB.cpp
  src/FastLocal.cpp
  src/FastNC.cpp
  src/FastT0.cpp
  src/FastT1.cpp
  src/FastT2.cpp
  src/HartTasks.cpp
  src/NRIUtils.cpp
  src/Printf.cpp
  src/T6CellO.cpp
  src/T6EPL1Data.cpp
  src/T6EPs.cpp
  src/T6ImageBlock.cpp
  src/T6Neighbor.cpp
  src/TEFs.cpp
  src/formatSymbols.pl
  src/t6main.cpp
#  src/RunLevel.cpp
)

# Assign global file IDs to crosslib files
#assign_file_ids(crosslib)
  
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
message(STATUS "AHAX: got SHARED_INCLUDE_DIRS = ${SHARED_INCLUDE_DIRS}")
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

# make crosslib depend on the loader script
add_dependencies(crosslib ${GENERATED_LDSCRIPT_TARGET_NAME})

# our hackogack objdump parser
set(PERL_FORMATTER_PATH "${CMAKE_SOURCE_DIR}/srcs/cross/src/formatSymbols.pl")
# and our hackogack size parser
set(PERL_SIZER_PATH "${CMAKE_SOURCE_DIR}/srcs/cross/src/formatSize.pl")

# --- Copy the generated header to a shared location ---
set(SHARED_GENERATED_HEADERS_DIR "${CMAKE_SOURCE_DIR}/srcs/generated_headers")

# Finally, suck in per-image cmakes
include(codes/CrossCodes.cmake)
message("BUILD ${CROSS_CODES_TO_BUILD}")

foreach(prog ${CROSS_CODES_TO_BUILD})
  include(codes/${prog}.cmake)
endforeach()

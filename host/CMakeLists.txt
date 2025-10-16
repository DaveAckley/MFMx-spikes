# Host specific build
set(PYBIND11_FINDPYTHON ON)
set(pybind11_DIR ../../../venv/lib/python3.10/site-packages/pybind11/share/cmake/pybind11)
find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(pymain STATIC NO_EXTRAS src/pymain.cpp)

# Add C++ compiler options specifically for pymain ---
target_compile_options(pymain PRIVATE
  -g          # Add debug symbols
  -O0         # Disable optimizations (often desired for debugging)
#  -Wall       # Enable all common warnings
#  -Wextra     # Enable extra warnings
  -Wno-unused-parameter  # stuff we want
  -Wno-unused-variable   # to live with
  # Add any other host-specific C++ flags here
)


add_executable(hostmain
  src/host_main.cpp # Assuming your host main file is here
  src/host_utility.cpp # Add other host-specific source files here
  src/spikeMain.cpp
  src/CodeManager.cpp
  src/FATAL.cpp
  src/FailStrings.cpp
  src/OurTLBs.cpp
  ${SHARED_SOURCES}
)

target_include_directories(hostmain PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${SHARED_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/generated_headers
)

# Add C++ compiler options specifically for hostmain ---
target_compile_options(hostmain PRIVATE
  -g          # Add debug symbols
  -O0         # Disable optimizations (often desired for debugging)
#  -Wall       # Enable all common warnings
#  -Wextra     # Enable extra warnings
  -Wno-unused-parameter  # stuff we want
  -Wno-unused-variable   # to live with
  # Add any other host-specific C++ flags here
)

# If you need to link specific libraries for hostmain
target_link_libraries(hostmain PRIVATE
    pymain
)

# If you also need specific linker options for hostmain (e.g., for system libraries)
# target_link_options(hostmain PRIVATE
#     -pthread # Example: Link with pthreads on host
# )

# If you need to link specific libraries for hostmain
# target_link_libraries(hostmain PRIVATE
#     some_host_library
# )

# target_sources(hostmain PRIVATE
#   src/host_utility.cpp # Add other host-specific source files here
#   ${CMAKE_SOURCE_DIR}/shared/src/shared_utility.cpp # Add shared source files here
# )

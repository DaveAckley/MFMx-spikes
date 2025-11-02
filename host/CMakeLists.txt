# host/CMakeLists.txt

# Create the Python module target
pybind11_add_module(MFMx src/mfmx_wrapper.cpp)

target_sources(MFMx PRIVATE
    src/spikeMain.cpp
    src/BlackHole.cpp
    src/C4I.cpp
    src/CodeManager.cpp
    src/FATAL.cpp
    src/FailStrings.cpp
    src/OurTLBs.cpp
    src/BHTag.cpp
    src/BHLog.cpp
    src/HostUtils.cpp
    ${SHARED_SOURCES}
)

# Add C++ compiler options specifically for pymain ---
target_compile_options(MFMx PRIVATE
  -g          # Add debug symbols
    #    -O0         # Disable optimizations (often desired for debugging)
    #  -Wall       # Enable all common warnings
    #  -Wextra     # Enable extra warnings
    -Wno-unused-parameter  # stuff we want
    -Wno-unused-variable   # to live with
    # Add any other host-specific C++ flags here
  )

  target_include_directories(MFMx PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/host/include
    ${SHARED_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/generated_headers
  )

  target_link_libraries(MFMx PRIVATE pybind11::headers)
  install(TARGETS MFMx DESTINATION mfmx)


# If you need to link specific libraries for hostmain
# target_link_libraries(hostmain PRIVATE
#    pymain
# )

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

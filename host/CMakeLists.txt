# Host specific build
add_executable(hostmain
  src/host_main.cpp # Assuming your host main file is here
  src/host_utility.cpp # Add other host-specific source files here
  ${SHARED_SOURCES}
)

target_include_directories(hostmain PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${SHARED_INCLUDE_DIRS}
)

# target_sources(hostmain PRIVATE
#   src/host_utility.cpp # Add other host-specific source files here
#   ${CMAKE_SOURCE_DIR}/shared/src/shared_utility.cpp # Add shared source files here
# )

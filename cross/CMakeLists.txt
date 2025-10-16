# Cross-compiled specific build
add_executable(crossmain
  src/cross_main.cpp # Assuming your cross main file is here
  src/cross_utility.cpp # Add other cross-specific source files here
  ${SHARED_SOURCES}
)

target_include_directories(crossmain PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${SHARED_INCLUDE_DIRS}
)


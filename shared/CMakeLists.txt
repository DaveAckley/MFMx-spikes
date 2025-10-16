# This file defines variables for shared sources and includes.
# It does NOT create any targets itself.

# Define Shared Sources
# Use CMAKE_CURRENT_SOURCE_DIR here as this file is within 'shared/'
set(SHARED_SOURCES_LIST
  ${CMAKE_CURRENT_SOURCE_DIR}/src/shared_utility.cpp
  # Add any other shared .cpp files here
)
# Make the variable available in the parent scope (the root CMakeLists.txt)
set(SHARED_SOURCES "${SHARED_SOURCES_LIST}" PARENT_SCOPE)

# Define Shared Include Directories
set(SHARED_INCLUDE_DIRS_LIST
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)
# Make the variable available in the parent scope
set(SHARED_INCLUDE_DIRS "${SHARED_INCLUDE_DIRS_LIST}" PARENT_SCOPE)

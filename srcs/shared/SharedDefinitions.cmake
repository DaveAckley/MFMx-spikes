# This file defines variables for shared sources and includes.
# It does NOT create any targets itself.
#message(FORG in ${CMAKE_CURRENT_SOURCE_DIR} do)
# Define Shared Sources
# CURRENT_SOURCE_DIR is project root, so get our dir
set(SHARED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/srcs/shared)
# Use CMAKE_CURRENT_SOURCE_DIR here as this file is within 'shared/'
set(SHARED_SOURCES_LIST
  ${SHARED_DIR}/src/BlockCode.cpp
  ${SHARED_DIR}/src/Dirs.cpp
  ${SHARED_DIR}/src/EP.cpp
  ${SHARED_DIR}/src/FailCodes.cpp
  ${SHARED_DIR}/src/ImageBlock.cpp
  ${SHARED_DIR}/src/ImageCode.cpp
  ${SHARED_DIR}/src/MDist.cpp
  ${SHARED_DIR}/src/P4Atom.cpp
  ${SHARED_DIR}/src/Point.cpp
  ${SHARED_DIR}/src/Random.cpp
  ${SHARED_DIR}/src/S8C.cpp
  ${SHARED_DIR}/src/mt19937.cpp
#  ${SHARED_DIR}/src/fastlz.cpp
#  ${SHARED_DIR}/src/U8C.cpp eaten by the monster of UxC
  # Add any other shared .cpp files here
)
set(SHARED_SOURCES "${SHARED_SOURCES_LIST}")

# Define Shared Include Directories
set(SHARED_INCLUDE_DIRS_LIST
  ${SHARED_DIR}/include
)
# Make the variable available in the parent scope
set(SHARED_INCLUDE_DIRS "${SHARED_INCLUDE_DIRS_LIST}")

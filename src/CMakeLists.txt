# src/CMakeLists.txt
# Create the Python module target
pybind11_add_module(MFMx mfmx_wrapper.cpp card.cpp)

# Link against pybind11 and any other necessary libraries
target_link_libraries(MFMx PRIVATE pybind11::embed) # Use pybind11::embed for standalone modules
target_compile_features(MFMx PRIVATE cxx_std_17) # Or your desired C++ standard

# This tells CMake (and scikit-build-core) to install the compiled .so file
install(TARGETS MFMx DESTINATION mfmx) # Installs the .so into 'site-packages/mfmx/'

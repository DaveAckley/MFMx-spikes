#include <pybind11/pybind11.h>
#include "card.hpp" // Include your C++ class header

namespace py = pybind11;

// This macro creates the Python module named 'MFMx'
PYBIND11_MODULE(MFMx, m) {
  m.doc() = "pybind11 example plugin for MFMx"; // Optional module docstring

  // Expose the Card class
  py::class_<mfmx::Card>(m, "Card")
    .def(py::init<int>(), py::arg("value")) // Constructor
    .def("getDescription", &mfmx::Card::getDescription); // Member function
}

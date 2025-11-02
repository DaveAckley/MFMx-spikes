#if 0
#include <string>
#include "Pet.h"
#include <pybind11/pybind11.h>

namespace py = pybind11;

PYBIND11_MODULE(example, m, py::mod_gil_not_used()) {
  py::class_<Pet>(m, "Pet")
    .def(py::init<const std::string &>())
    .def("setName", &Pet::setName)
    .def("getName", &Pet::getName);
}
#endif

#include <iostream>
#include <pybind11/pybind11.h>
#include "Pet.h"
#include "BlackHole.h"

namespace py = pybind11;

#define STRINGIFY(x) #x
#define MACRO_STRINGIFY(x) STRINGIFY(x)

extern int spikeMain();

int run() {
  Pet p("clo9cnk");
  p.setName("ZONG");
  return spikeMain();
}

int add(int i, int j) {
    return i * j + 1;
}

int nsqr(int i) {
    return - i * i;
}

PYBIND11_MODULE(MFMx, m, py::mod_gil_not_used(), py::multiple_interpreters::per_interpreter_gil()) {
    m.doc() = R"pbdoc(
        mfmx example plugin
        -----------------------

        .. currentmodule:: mfmx

        .. autosummary::
           :toctree: _generate

           add
           subtract
    )pbdoc";

    MFM::BlackHole::pybindings(m);
    
    py::class_<Pet>(m,"Pet")
      .def(py::init<const std::string &>())
      .def("setName", &Pet::setName)
      .def("getName", &Pet::getName);

    m.def("add", &add, R"pbdoc(
        Add two numbers

        Some other explanation about the add function.
    )pbdoc");

    m.def("nsqr", &nsqr, R"pbdoc(
        Nsqr a number

        No other explanation about the nsqr function.
    )pbdoc");

    m.def("subtract", [](int i, int j) { return i - j; }, R"pbdoc(
        Subtract two numbers

        Some other explanation about the subtract function.
    )pbdoc");

    //    auto m_a = m.def_submodule("c4i_mfm","The terminal command interface");

    //    m_a.def("run", &c4i_mfm::run, R"pbdoc(
    m.def("run", &run, R"pbdoc(
        Run shit

    )pbdoc");

#ifdef VERSION_INFO
    m.attr("__version__") = MACRO_STRINGIFY(VERSION_INFO);
#else
    m.attr("__version__") = "dev";
#endif
}

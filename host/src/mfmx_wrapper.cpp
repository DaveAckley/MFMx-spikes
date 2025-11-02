#include <pybind11/pybind11.h>
#include "BHTag.h" 
#include "BHLog.h" 
#include "BlackHole.h" 

namespace py = pybind11;

// This macro creates the Python module named 'MFMx'
PYBIND11_MODULE(MFMx, m) {
  m.doc() = "pybind11 bindings for MFMx such as they is";

  // Expose the BHTag class
  MFM::BHTag::pybindings(m);

  // Expose the BHLog class
  MFM::BHLog::pybindings(m);

  // Expose the BlackHole class
  MFM::BlackHole::pybindings(m);
}

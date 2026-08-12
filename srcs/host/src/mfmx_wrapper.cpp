#include <pybind11/pybind11.h>
#include "BHTag.h" 
#include "BHLog.h" 
#include "Blackhole.h" 
#include "EWControl.h" 
#include "T6Image.h" 
#include "HostUtils.h" 
#include "ImageManager.h" 
#include "BGRImage.h" 
#include "QuietBox.h" 
#include "t6-exports.h" // for getMFMxModuleVersion()

namespace py = pybind11;

// This macro creates the Python module named 'MFMx'
PYBIND11_MODULE(MFMx, m) {
  m.doc() = "pybind11 bindings for MFMx such as they is";

  // Init the HostUtils (logging, clock, ...) at module load time
  MFM::initHostUtils();         // (that's now, right?)

  // Expose the simulation directory
  m.def("getSimDir", &MFM::getSimDir);  

  // Expose the MFMx version string
  m.def("getVersion", &MFM::T6::getMFMxModuleVersion);  

  // Expose the BHTag class
  MFM::BHTag::pybindings(m);

  // Expose the BHLog class
  MFM::BHLog::pybindings(m);

  // Expose the Blackhole class
  MFM::Blackhole::pybindings(m);

  // Expose the EWControl class
  MFM::EWControl::pybindings(m);

  // Expose the T6Image class
  MFM::T6Image::pybindings(m);

  // Expose the Layout class
  MFM::Layout::pybindings(m);

  // Expose the ImageManager class
  MFM::ImageManager::pybindings(m);

  // Expose the bullshit non-singleton access-to-ImageManager class
  MFM::NSIM::pybindings(m);

  // Expose the BGRImageHD class
  MFM::BGRImageHD::pybindings(m);

  // Expose the QuietBox class
  MFM::QuietBox::pybindings(m);
  
}

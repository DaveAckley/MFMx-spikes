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
#include "LiveAER.h" // live mAER feed
#include "ZHostDecompressor.h" // report stream health
#include "EthSeamHostPy.h" // eth-seam E1 management
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

  // Latest cumulative events per hub (see LiveAER.h): (seeded, [(key,events,ms)])
  m.def("liveAERSnapshot", []() {
    bool seeded = false;
    std::vector<MFM::LiveAER::HubEvents> v =
      MFM::LiveAER::get().snapshot(seeded);
    py::list hubs;
    for (const auto & h : v)
      hubs.append(py::make_tuple(h.mKey, h.mEvents, h.mHubMs));
    return py::make_tuple(seeded, hubs);
  });
  m.def("liveAERClear", []() { MFM::LiveAER::get().clear(); });

  // Health of every hub's compressed atom-report stream (see ZHDHealth):
  // [(chip, tlbi, reports, invalid, badAtoms, consecInvalid, desyncs,
  //   desynced, desyncAtReport, desyncAtCBytes, desyncAtPacket, kind)]
  m.def("zhdSnapshot", []() {
    py::list hubs;
    for (const auto & h : MFM::ZHostDecompressor::snapshotHealth())
      hubs.append(py::make_tuple(h.mChip, h.mTLBI, h.mReports, h.mInvalid,
                                 h.mBadAtoms, h.mConsecInvalid, h.mDesyncs,
                                 h.mDesynced, h.mDesyncAtReport,
                                 h.mDesyncAtCBytes, h.mDesyncAtPacket,
                                 h.mDesyncKind));
    return hubs;
  });

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

  // Expose the eth-seam E1 manager
  MFM::EthSeamHostPy::pybindings(m);
  
}

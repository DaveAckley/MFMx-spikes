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
  //   desynced, desyncAtReport, desyncAtCBytes, desyncAtPacket, kind,
  //   restarts, restartPending, staleDropped)]
  m.def("zhdSnapshot", []() {
    py::list hubs;
    for (const auto & h : MFM::ZHostDecompressor::snapshotHealth())
      hubs.append(py::make_tuple(h.mChip, h.mTLBI, h.mReports, h.mInvalid,
                                 h.mBadAtoms, h.mConsecInvalid, h.mDesyncs,
                                 h.mDesynced, h.mDesyncAtReport,
                                 h.mDesyncAtCBytes, h.mDesyncAtPacket,
                                 h.mDesyncKind, h.mRestarts,
                                 h.mRestartPending, h.mStaleDropped));
    return hubs;
  });
  // Who last painted pixel (x,y) of the picture (QuietBox::whoPainted):
  // None if nobody has, else (chip, tlbi, ring, epochKnown, epoch, atomType,
  // newestEpoch or -1, gridX, gridY of the site in the painting hub).  For hunting ghost pixels.
  m.def("whoPainted", [](unsigned x, unsigned y) -> py::object {
    if (x >= 1920u || y >= 1080u) return py::none();
    const auto p = MFM::QuietBox::whoPainted(y*1920u + x);
    if (!p.mValid) return py::none();
    const unsigned ne = MFM::QuietBox::newestEpoch.load();
    return py::make_tuple(p.mChip, p.mTLBI, p.mRing, p.mEpochKnown, p.mEpoch,
                          p.mType, (ne & 0x10000u) ? (int) (ne & 0xffffu) : -1,
                          p.mGX, p.mGY);
  });
  // The last few reports that reached pixel (x,y), newest first: a list of
  // (accepted, ring, atomType, changeEpoch, phase) (phase 1 = events; accepted False = dropped as stale).
  m.def("pixelHistory", [](unsigned x, unsigned y) -> py::list {
    py::list out;
    if (x >= 1920u || y >= 1080u) return out;
    for (unsigned k = 0u; k < MFM::QuietBox::PIXEL_HISTORY; ++k) {
      const unsigned v = MFM::QuietBox::getHistory(y*1920u + x,k);
      if (!(v >> 31)) break;
      out.append(py::make_tuple((bool) ((v >> 30) & 1u), (bool) ((v >> 29) & 1u),
                                (v >> 20) & 0x1ffu, v & 0x7fffu,
                                (v >> 16) & 1u));
    }
    return out;
  });
  // Which hub OWNS pixel (x,y) (the hub whose SELF sites map onto it): None
  // if the pixel has no hub, else (chip, tlbi, gridX, gridY) with the grid
  // site of that pixel in the owner hub.  For zoom specks: a RING painter's
  // value is only the borrower's cache copy, the owner's SELF is the truth.
  m.def("pixelOwner", [](unsigned x, unsigned y) -> py::object {
    if (x >= 1920u || y >= 1080u) return py::none();
    MFM::QuietBox & qb = MFM::QuietBox::get();
    const MFM::T6GridInfo & t6i = qb.getT6GridInfoFor(MFM::DG::Coord(x,y));
    if (!t6i.mDGAddress.isValid()) return py::none();
    const unsigned gx = x - t6i.mT6GridOrigin.x + MFM::T6Grid::SELF_ORIGIN.x;
    const unsigned gy = y - t6i.mT6GridOrigin.y + MFM::T6Grid::SELF_ORIGIN.y;
    return py::make_tuple(t6i.mDGAddress.mChipNum, t6i.mTLBI, gx, gy);
  });
  // Ask the hubs behind pixel (x,y) to re-report it: the hub that last painted
  // it (a RING painter holds the borrowed copy) and its owner (the SELF site).
  // Returns how many hubs were asked.  Answers repaint the pixel (host-directed
  // refresh; the hub queues the site and reports its current atom).
  m.def("refreshPixel", [](unsigned x, unsigned y) -> unsigned {
    if (x >= 1920u || y >= 1080u) return 0u;
    unsigned n = 0u;
    const auto p = MFM::QuietBox::whoPainted(y*1920u + x);
    if (p.mValid &&
        MFM::ZHostDecompressor::requestRefresh(p.mChip,p.mTLBI,p.mGX,p.mGY)) ++n;
    MFM::QuietBox & qb = MFM::QuietBox::get();
    const MFM::T6GridInfo & t6i = qb.getT6GridInfoFor(MFM::DG::Coord(x,y));
    if (t6i.mDGAddress.isValid()) {
      const unsigned gx = x - t6i.mT6GridOrigin.x + MFM::T6Grid::SELF_ORIGIN.x;
      const unsigned gy = y - t6i.mT6GridOrigin.y + MFM::T6Grid::SELF_ORIGIN.y;
      if (!(p.mValid && p.mChip == t6i.mDGAddress.mChipNum &&
            p.mTLBI == t6i.mTLBI && p.mGX == gx && p.mGY == gy) &&
          MFM::ZHostDecompressor::requestRefresh(t6i.mDGAddress.mChipNum,
                                                 t6i.mTLBI,gx,gy)) ++n;
    }
    return n;
  });
  // (asked, answered, caught): refresh sites requested, reports that
  // answered them, and answers that emptied a pixel that showed an atom.
  m.def("refreshStats", []() {
    unsigned long long a, b, c;
    MFM::ZHostDecompressor::refreshStats(a,b,c);
    return py::make_tuple(a,b,c);
  });
  // TEST: lose one compressed byte of one hub's report stream; True if it exists.
  m.def("zhdBreak", [](unsigned chip, unsigned tlbi) {
    return MFM::ZHostDecompressor::injectDrop(chip, tlbi);
  });
  // Whether a desynced hub is asked to restart its stream (default on).
  m.def("zhdRestartEnable", [](bool on) {
    MFM::ZHostDecompressor::setRestartEnabled(on);
  });
  m.def("zhdRestartEnabled", []() {
    return MFM::ZHostDecompressor::restartEnabled();
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

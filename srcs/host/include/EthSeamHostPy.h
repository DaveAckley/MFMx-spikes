#pragma once   /* -*- C++ -*- */
/** EthSeamHostPy.h -- python glue for EthSeamHost (the eth-seam E1 management, EthLinks.h / EthSeamHost.h).
    Included only by mfmx_wrapper.cpp.  NOT COMPILED where it was written (no pybind11 on that machine): it uses only API patterns that
    already appear in QuietBox.h / Blackhole.h, but treat the first build on the QuietBox as its compile test.

    Python (module MFMx):
      h = MFMx.EthSeamHost.get()
      err = h.bringUpLink(chipA, chipB, ethx, "/path/e1fwd16.bin",
                          channelsA=[(channel, legs, t6x, t6y, t6base), ...], channelsB=[...],
                          flags=2, ringCars=4, force=False, smoke=True)       # "" on success, else the error text
      h.status();  h.health(chip, ethx);  h.channelStat(chip, ethx, channel, wordIndex)
      h.declareSeam(chipA, dir4, chipB, [ethx, ...])  # BEFORE deploy;
                                  # dir4: N=0 W=1 S=2 E=3, chipB is that way
      h.tearDownLink(chipA, chipB, ethx);  h.tearDownAll()
    legs: 1 = OUT (T6 -> link), 2 = IN_NOC (link -> local T6 at t6x,t6y @ t6base), 4 = IN_COUNT.  flags: 1 fence, 2 CSR (default), 8 VC_STATIC, 16 TXQFIX,
    32 NoC1.  Call it after both chips' T6 images are deployed and before the T6s are released; Blackhole::closeChip tears the links down automatically. */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "EthSeamHost.h"
#include "QuietBox.h"
#include "Blackhole.h"
#include "HostUtils.h"

#include <map>
#include <tuple>

namespace py = pybind11;

namespace MFM {
  namespace EthSeamHostPy {

    using ChannelTuple = std::tuple<u32, u32, u32, u32, u32>;     // channel, legs, t6x, t6y, t6base

    inline EthChannelSetup makeSetup(const std::vector<ChannelTuple> & v, u32 flags, u32 ringCars) {
      EthChannelSetup s;
      s.flags = (u8) flags;
      s.ringCars = (u8) ringCars;
      for (const ChannelTuple & t : v) {
        EthChannel c;
        c.channel = (u8) std::get<0>(t);
        c.legs = (u8) std::get<1>(t);
        c.t6x = (u8) std::get<2>(t);
        c.t6y = (u8) std::get<3>(t);
        c.t6Base = std::get<4>(t);
        s.channels.push_back(c);
      }
      return s;
    }

    inline void install() {
      EthSeamHost::get().setPortFactory([](u32 chip, std::string & err) -> std::unique_ptr<EthPort> {
        Blackhole * bh = QuietBox::get().getBlackholeIfPresent(chip);
        if (!bh) { err = "no Blackhole " + std::to_string(chip); return nullptr; }
        if (bh->getStep() < Blackhole::Step::HAS_OPEN_DEVICE) { err = "Blackhole " + std::to_string(chip) + " device is not open yet"; return nullptr; }
        s32 fd = bh->getChipFD();
        if (fd < 0) { err = "Blackhole " + std::to_string(chip) + " has no device fd"; return nullptr; }
        return std::unique_ptr<EthPort>(new KmdEthPort(fd));
      });
      ethLinksSetLogger([](const std::string & s) { HTprintf("[EthLinks] %s\n", s.c_str()); });
    }

    inline void pybindings(py::module & m) {
      install();
      py::class_<EthSeamHost> c(m, "EthSeamHost");
      c.def_static("get", &EthSeamHost::get, py::return_value_policy::reference);
      c.def("bringUpLink", [](EthSeamHost & h, u32 chipA, u32 chipB, u32 ethx, const std::string & image,
                              const std::vector<ChannelTuple> & chA, const std::vector<ChannelTuple> & chB,
                              u32 flags, u32 ringCars, bool force, bool smoke) -> std::string {
        return h.bringUpLink(chipA, chipB, ethx, image, makeSetup(chA, flags, ringCars), makeSetup(chB, flags, ringCars), force, smoke);
      }, py::arg("chipA"), py::arg("chipB"), py::arg("ethx"), py::arg("image"), py::arg("channelsA"), py::arg("channelsB"),
         py::arg("flags") = (u32) EthSeam::E1F_CSR, py::arg("ringCars") = (u32) EthSeam::RING_CARS, py::arg("force") = false, py::arg("smoke") = true,
         py::call_guard<py::gil_scoped_release>());
      c.def("declareSeam", [](EthSeamHost & h,
                              u32 chipA,
                              u32 dir4,
                              u32 chipB,
                              const std::vector<u32> & ethx) -> std::string {
        std::vector<u8> x;
        for (u32 v : ethx) x.push_back((u8) v);
        return h.declareSeam(chipA, dir4, chipB, x);
      }, py::arg("chipA"), py::arg("dir4"), py::arg("chipB"),
         py::arg("ethx"));
      c.def("clearSeams", &EthSeamHost::clearSeams);
      c.def("tearDownLink", &EthSeamHost::tearDownLink, py::call_guard<py::gil_scoped_release>());
      c.def("tearDownAll", &EthSeamHost::tearDownAll, py::call_guard<py::gil_scoped_release>());
      c.def("status", &EthSeamHost::status, py::call_guard<py::gil_scoped_release>());
      c.def("health", [](EthSeamHost & h, u32 chip, u32 ethx) -> std::map<std::string, u32> {
        EthHealth e; std::string err;
        std::map<std::string, u32> r;
        if (!h.health(chip, ethx, e, err)) { r["ok"] = 0; return r; }
        r["ok"] = 1; r["port"] = e.port; r["heartbeat"] = e.heartbeat; r["retrain"] = e.retrain;
        r["fecCorrected"] = e.fecCorrected; r["fecUncorrected"] = e.fecUncorrected;
        return r;
      }, py::call_guard<py::gil_scoped_release>());
      c.def("channelStat", [](EthSeamHost & h, u32 chip, u32 ethx, u32 channel, u32 wordIndex) -> s64 {
        u32 v = 0; std::string err;
        return h.channelStat(chip, ethx, channel, wordIndex, v, err) ? (s64) v : (s64) -1;
      }, py::call_guard<py::gil_scoped_release>());
    }

  } // namespace EthSeamHostPy
} // namespace MFM

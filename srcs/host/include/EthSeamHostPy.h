#pragma once   /* -*- C++ -*- */
/** EthSeamHostPy.h -- python glue for EthSeamHost (the eth-seam E1
    management, EthLinks.h / EthSeamHost.h).  Included only by
    mfmx_wrapper.cpp.

    Python (from mfmx import MFMx):
      h = MFMx.EthSeamHost.get()

      # BEFORE deploying T6 images.  dir4: N=0 W=1 S=2 E=3, chipB
      # lies that way from chipA.
      h.declareSeam(chipA, dir4, chipB, [ethx, ...])

      # after both chips' T6 images are deployed, before the T6s
      # are released.  "" on success, else the error text.
      err = h.bringUpLink(chipA, chipB, ethx, "/path/e1fwd16.bin",
                          channelsA=[(channel, legs, t6x, t6y,
                                      t6base), ...],
                          channelsB=[...],
                          flags=2,
                          ringCars=4,
                          force=False,
                          smoke=True)

      # Or, with seams declared and the T6 images deployed: plan the
      # channels from the deployed InterHub addresses and bring up every
      # live link of every declared seam.  "" on success.
      err = h.bringUpSeams("/path/e1fwd16.bin",
                           flags=2, ringCars=4, force=False, smoke=True)

      h.status()
      h.health(chip, ethx)
      h.channelsOf(chip, ethx)          # channel numbers on that link
      h.channelStat(chip, ethx, channel, wordIndex)
      h.tearDownLink(chipA, chipB, ethx)
      h.tearDownAll()

    legs: 1 = OUT (T6 -> link), 2 = IN_NOC (link -> local T6 at
    t6x,t6y @ t6base), 4 = IN_COUNT.
    flags: 1 fence, 2 CSR (default), 8 VC_STATIC, 16 TXQFIX, 32 NoC1.

    Blackhole::closeChip tears the links down automatically. */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include "EthSeamHost.h"
#include "QuietBox.h"
#include "Blackhole.h"
#include "HostUtils.h"
#include "T6Image.h"
#include "BlockCode.h"
#include "UxC.h"

#include <map>
#include <tuple>

namespace py = pybind11;

namespace MFM {
  namespace EthSeamHostPy {

    // channel, legs, t6x, t6y, t6base
    using ChannelTuple = std::tuple<u32, u32, u32, u32, u32>;

    inline EthChannelSetup makeSetup(const std::vector<ChannelTuple> & v,
                                     u32 flags,
                                     u32 ringCars) {
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

    inline std::unique_ptr<EthPort> makePort(u32 chip,
                                             std::string & err) {
      std::string tag = "Blackhole " + std::to_string(chip);
      Blackhole * bh = QuietBox::get().getBlackholeIfPresent(chip);
      if (!bh) {
        err = "no " + tag;
        return nullptr;
      }
      if (bh->getStep() < Blackhole::Step::HAS_OPEN_DEVICE) {
        err = tag + " device is not open yet";
        return nullptr;
      }
      s32 fd = bh->getChipFD();
      if (fd < 0) {
        err = tag + " has no device fd";
        return nullptr;
      }
      return std::unique_ptr<EthPort>(new KmdEthPort(fd));
    }

    /* The hub at CT6 (ct6x, ct6y): its NoC0 and the L1 base of its
       deployed InterHub storages, from the image deployed there. */
    inline bool lookupHub(u32 chip,
                          u32 ct6x,
                          u32 ct6y,
                          EthSeamHost::SeamHub & hub,
                          std::string & err) {
      std::string tag = "Blackhole " + std::to_string(chip)
        + " hub CT6 (" + std::to_string(ct6x)
        + "," + std::to_string(ct6y) + ")";
      Blackhole * bh = QuietBox::get().getBlackholeIfPresent(chip);
      if (!bh) {
        err = "no Blackhole " + std::to_string(chip);
        return false;
      }
      U8C ct6((u8) ct6x, (u8) ct6y);
      u32 tlbi = U8C::makeTLBIFromCT6Coord(ct6);
      U8C noc0 = U8C::makeNoC0CoordFromCT6Coord(ct6);
      const T6Image * img =
        bh->getOurTLBs().getTLBInfo(tlbi).getDeployedImageIfAny();
      if (!img) {
        err = tag + ": no image deployed yet";
        return false;
      }
      ImageBlockAddr * iba =
        img->getImageBlockAddrForBlockCodeIfAny(BC_INTERHUB);
      if (!iba) {
        err = tag + ": image '" + img->getName()
          + "' has no InterHub block";
        return false;
      }
      hub.noc0x = noc0.x;
      hub.noc0y = noc0.y;
      hub.ihubBase = iba->getBlockAddr();
      ImageBlockAddr * cba =
        img->getImageBlockAddrForBlockCodeIfAny(BC_CORNERHUB);
      hub.chubBase = cba ? cba->getBlockAddr() : 0u;
      return true;
    }

    inline void install() {
      EthSeamHost::get().setPortFactory(makePort);
      EthSeamHost::get().setHubLookup(lookupHub);
      ethLinksSetLogger([](const std::string & s) {
        HTprintf("[EthLinks] %s\n", s.c_str());
      });
    }

    inline std::map<std::string, u32> healthMap(EthSeamHost & h,
                                                u32 chip,
                                                u32 ethx) {
      EthHealth e;
      std::string err;
      std::map<std::string, u32> r;
      if (!h.health(chip, ethx, e, err)) {
        r["ok"] = 0;
        return r;
      }
      r["ok"] = 1;
      r["port"] = e.port;
      r["heartbeat"] = e.heartbeat;
      r["retrain"] = e.retrain;
      r["fecCorrected"] = e.fecCorrected;
      r["fecUncorrected"] = e.fecUncorrected;
      return r;
    }

    // {"ok": 0|1, "port": status}; ok 0 if the chip cannot be read
    inline std::map<std::string, u32> portStatusMap(EthSeamHost & h,
                                                    u32 chip,
                                                    u32 ethx) {
      std::map<std::string, u32> r;
      u32 port = 0;
      std::string err;
      if (!h.portStatus(chip, ethx, port, err)) {
        r["ok"] = 0;
        return r;
      }
      r["ok"] = 1;
      r["port"] = port;
      return r;
    }

    inline void pybindings(py::module & m) {
      install();
      py::class_<EthSeamHost> c(m, "EthSeamHost");
      c.def_static("get",
                   &EthSeamHost::get,
                   py::return_value_policy::reference);

      c.def("bringUpLink",
            [](EthSeamHost & h,
               u32 chipA,
               u32 chipB,
               u32 ethx,
               const std::string & image,
               const std::vector<ChannelTuple> & chA,
               const std::vector<ChannelTuple> & chB,
               u32 flags,
               u32 ringCars,
               bool force,
               bool smoke) -> std::string {
              EthChannelSetup sa = makeSetup(chA, flags, ringCars);
              EthChannelSetup sb = makeSetup(chB, flags, ringCars);
              return h.bringUpLink(chipA,
                                   chipB,
                                   ethx,
                                   image,
                                   sa,
                                   sb,
                                   force,
                                   smoke);
            },
            py::arg("chipA"),
            py::arg("chipB"),
            py::arg("ethx"),
            py::arg("image"),
            py::arg("channelsA"),
            py::arg("channelsB"),
            py::arg("flags") = (u32) EthSeam::E1F_CSR,
            py::arg("ringCars") = (u32) EthSeam::RING_CARS,
            py::arg("force") = false,
            py::arg("smoke") = true,
            py::call_guard<py::gil_scoped_release>());

      c.def("declareSeam",
            [](EthSeamHost & h,
               u32 chipA,
               u32 dir4,
               u32 chipB,
               const std::vector<u32> & ethx) -> std::string {
              std::vector<u8> x;
              for (u32 v : ethx) x.push_back((u8) v);
              return h.declareSeam(chipA, dir4, chipB, x);
            },
            py::arg("chipA"),
            py::arg("dir4"),
            py::arg("chipB"),
            py::arg("ethx"));
      c.def("clearSeams", &EthSeamHost::clearSeams);
      c.def("bringUpSeams",
            &EthSeamHost::bringUpSeams,
            py::arg("image"),
            py::arg("flags") = (u32) EthSeam::E1F_CSR,
            py::arg("ringCars") = (u32) EthSeam::RING_CARS,
            py::arg("force") = false,
            py::arg("smoke") = true,
            py::call_guard<py::gil_scoped_release>());

      c.def("tearDownLink",
            &EthSeamHost::tearDownLink,
            py::call_guard<py::gil_scoped_release>());
      c.def("tearDownAll",
            &EthSeamHost::tearDownAll,
            py::call_guard<py::gil_scoped_release>());
      c.def("status",
            &EthSeamHost::status,
            py::call_guard<py::gil_scoped_release>());
      c.def("health",
            &healthMap,
            py::call_guard<py::gil_scoped_release>());

      c.def("portStatus",
            &portStatusMap,
            py::arg("chip"),
            py::arg("ethx"),
            py::call_guard<py::gil_scoped_release>());

      c.def("releaseIdlePorts",
            &EthSeamHost::releaseIdlePorts,
            py::call_guard<py::gil_scoped_release>());

      c.def("channelsOf",
            &EthSeamHost::channelsOf,
            py::arg("chip"),
            py::arg("ethx"),
            py::call_guard<py::gil_scoped_release>());

      c.def("channelStat",
            [](EthSeamHost & h,
               u32 chip,
               u32 ethx,
               u32 channel,
               u32 wordIndex) -> s64 {
              u32 v = 0;
              std::string err;
              bool ok = h.channelStat(chip,
                                      ethx,
                                      channel,
                                      wordIndex,
                                      v,
                                      err);
              return ok ? (s64) v : (s64) -1;
            },
            py::call_guard<py::gil_scoped_release>());
    }

  } // namespace EthSeamHostPy
} // namespace MFM

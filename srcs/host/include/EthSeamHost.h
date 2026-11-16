#pragma once   /* -*- C++ -*- */
/** EthSeamHost.h -- the host-process owner of the eth-seam links (EthLinks.h).  One instance per process.
    NO pybind and no Blackhole/QuietBox dependency here (so it can be unit-tested natively): the glue (EthSeamHostPy.h) tells it
    how to get a chip's device fd; Blackhole::closeChip calls chipClosing() so a link is always torn down, and its TLB windows freed,
    BEFORE the fd they belong to is closed (an ioctl on a reused fd number could otherwise hit the wrong device).

    Intended use from mfmxctl (stage 3c, ERISC_SEAM_DESIGN.md): after both chips' T6 images are deployed (so the far T6 storage
    addresses are known) and BEFORE the T6s are released (startMachine / HAS_T6_CODE_RUNNING), call bringUpLink() for each live
    link; call tearDownLink()/tearDownAll() when stopping.  Nothing in MFMx calls it yet. */

#include "EthLinks.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace MFM {

  class EthSeamHost {
  public:
    using PortFactory = std::function<std::unique_ptr<EthPort>(u32 chip, std::string & err)>;

    static EthSeamHost & get() ;

    /** How to make the EthPort of a chip (the glue installs a KmdEthPort over the Blackhole's fd). */
    void setPortFactory(PortFactory f) ;

    /** Bring one link up (EthLink::bringUp) and remember it.  Returns "" on success, else the error text. */
    std::string bringUpLink(u32 chipA, u32 chipB, u32 ethx, const std::string & imagePath,
                            const EthChannelSetup & setupA, const EthChannelSetup & setupB, bool force, bool smoke) ;
    std::string tearDownLink(u32 chipA, u32 chipB, u32 ethx) ;       //< "" ok, or "no such link"
    void tearDownAll() ;
    void chipClosing(u32 chip) ;                                      //< tear down every link on this chip and drop its port; call BEFORE close(fd)

    std::string status() ;                                            //< one line per link
    bool health(u32 chip, u32 ethx, EthHealth & h, std::string & err) ;
    bool channelStat(u32 chip, u32 ethx, u32 channel, u32 wordIndex, u32 & value, std::string & err) ;

  private:
    struct Rec { u32 a, b, ethx; std::unique_ptr<EthLink> link; };
    EthPort * portFor(u32 chip, std::string & err) ;                  //< creates on first use; mMutex held
    Rec * find(u32 a, u32 b, u32 ethx) ;
    Rec * findEnd(u32 chip, u32 ethx, EthLinkEnd *& end) ;
    void closeRec(Rec & r) ;

    std::mutex mMutex;
    PortFactory mFactory;
    std::map<u32, std::unique_ptr<EthPort>> mPorts;
    std::vector<Rec> mLinks;
  };

} // namespace MFM

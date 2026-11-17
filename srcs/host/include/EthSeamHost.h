#pragma once   /* -*- C++ -*- */
/** EthSeamHost.h -- the host-process owner of the eth-seam links
    (EthLinks.h).  One instance per process.

    NO pybind and no Blackhole/QuietBox dependency here (so it can be
    unit-tested natively): the glue (EthSeamHostPy.h) tells it how to
    get a chip's device fd.  Blackhole::closeChip calls chipClosing()
    so a link is always torn down, and its TLB windows freed, BEFORE
    the fd they belong to is closed (an ioctl on a reused fd number
    could otherwise hit the wrong device).

    Intended use from mfmxctl (stage 3c, ERISC_SEAM_DESIGN.md):
      1. declareSeam() for each neighbor pair, BEFORE deploying the
         T6 images (CodeManager copies seamMaskFor() into each T6's
         HostBlock);
      2. deploy the T6 images (so the far T6 storage addresses are
         known);
      3. bringUpLink() for each live link, BEFORE the T6s are
         released (startMachine / HAS_T6_CODE_RUNNING);
      4. tearDownLink() / tearDownAll() when stopping. */

#include "EthLinks.h"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace MFM {

  class EthSeamHost {
  public:
    using PortFactory =
      std::function<std::unique_ptr<EthPort>(u32 chip,
                                             std::string & err)>;

    /** A hub T6 as the host sees it after deploy. */
    struct SeamHub {
      u8 noc0x;                 //< hub T6 NoC0 x
      u8 noc0y;                 //< hub T6 NoC0 y
      u32 ihubBase;             //< L1 address of its BC_INTERHUB blocks
      u32 chubBase = 0;         //< L1 address of its BC_CORNERHUB blocks
                                //  (0 if the image has none)
    };

    /** Find the hub at CT6 (ct6x, ct6y) of a chip: its NoC0 and the
        L1 base of its deployed InterHub storages.  The glue installs
        this over the Blackhole's deployed images. */
    using HubLookup =
      std::function<bool(u32 chip,
                         u32 ct6x,
                         u32 ct6y,
                         SeamHub & hub,
                         std::string & err)>;

    static EthSeamHost & get() ;

    /** Declare, BEFORE the T6 images are deployed, that chipB lies
        across direction dir4 (a Dir4: N=0 W=1 S=2 E=3) of chipA, joined by
        links at these ETH x coordinates (the same x on both cards).
        Each x must be in the cabling list for that axis
        (EthSeam::SEAM_LINKS_EW / NS); the order given does not
        matter, link order is always the cabling order.  Both
        directions are recorded.  Returns "" or an error text. */
    std::string declareSeam(u32 chipA,
                            u32 dir4,
                            u32 chipB,
                            const std::vector<u8> & ethx) ;
    void clearSeams() ;

    /// The HostBlock link-mask word for a chip (EthSeam
    /// packSeamMask layout); 0 if none declared
    u16 seamMaskFor(u32 chip) ;

    void setHubLookup(HubLookup f) ;

    /** Work out both ends' channel setups for ONE link of a declared
        seam: for each Dir4 storage dealt to that link, the receiving
        hub on each card and its InterHub storage address (needs the
        hub lookup, so call after deploy).  Returns "" or an error. */
    std::string planLink(u32 chipA,
                         u32 chipB,
                         u32 ethx,
                         u32 flags,
                         u32 ringCars,
                         EthChannelSetup & setupA,
                         EthChannelSetup & setupB) ;

    /** Plan and bring up every live link of every declared seam.  On
        any failure the links this call brought up are torn down
        again.  Returns "" or an error text. */
    std::string bringUpSeams(const std::string & imagePath,
                             u32 flags,
                             u32 ringCars,
                             bool force,
                             bool smoke) ;

    /** How to make the EthPort of a chip (the glue installs a
        KmdEthPort over the Blackhole's fd). */
    void setPortFactory(PortFactory f) ;

    /** Bring one link up (EthLink::bringUp) and remember it.
        Returns "" on success, else the error text. */
    std::string bringUpLink(u32 chipA,
                            u32 chipB,
                            u32 ethx,
                            const std::string & imagePath,
                            const EthChannelSetup & setupA,
                            const EthChannelSetup & setupB,
                            bool force,
                            bool smoke) ;

    /// "" ok, or "no such link"
    std::string tearDownLink(u32 chipA, u32 chipB, u32 ethx) ;
    void tearDownAll() ;

    /// tear down every link on this chip and drop its port; call
    /// BEFORE close(fd)
    void chipClosing(u32 chip) ;

    /// one line per link
    std::string status() ;

    /** The E1 channel numbers a live link carries (same on both ends),
        empty if there is no such link.  Use with channelStat. */
    std::vector<u32> channelsOf(u32 chip, u32 ethx) ;

    bool health(u32 chip,
                u32 ethx,
                EthHealth & h,
                std::string & err) ;
    bool channelStat(u32 chip,
                     u32 ethx,
                     u32 channel,
                     u32 wordIndex,
                     u32 & value,
                     std::string & err) ;

  private:
    struct Rec {
      u32 a;
      u32 b;
      u32 ethx;
      std::vector<u32> chans;   //< channel numbers set up on this link
      std::unique_ptr<EthLink> link;
    };

    struct Decl {
      u32 chipA;
      u32 dir4;                 //< chipB lies this way from chipA
      u32 chipB;
    };

    std::string bringUpLinkLocked(u32 chipA,
                                  u32 chipB,
                                  u32 ethx,
                                  const std::string & imagePath,
                                  const EthChannelSetup & setupA,
                                  const EthChannelSetup & setupB,
                                  bool force,
                                  bool smoke) ;
    std::string planLinkLocked(u32 chipA,
                               u32 chipB,
                               u32 ethx,
                               u32 flags,
                               u32 ringCars,
                               EthChannelSetup & setupA,
                               EthChannelSetup & setupB) ;
    bool planChannel(u32 a,
                     u32 b,
                     const EthSeam::SeamTable & ta,
                     const EthSeam::SeamTable & tb,
                     Dir4 dirAB,
                     u32 link,
                     u32 c,
                     EthChannel & ca,
                     EthChannel & cb,
                     std::string & err) ;
    std::string tearDownLinkLocked(u32 chipA, u32 chipB, u32 ethx) ;
    EthSeam::SeamTable tableLocked(u32 chip) ;
    std::string closureErrorLocked() ;

    /// creates on first use; mMutex held
    EthPort * portFor(u32 chip, std::string & err) ;

    Rec * find(u32 a, u32 b, u32 ethx) ;
    Rec * findEnd(u32 chip, u32 ethx, EthLinkEnd *& end) ;
    void closeRec(Rec & r) ;

    std::mutex mMutex;
    PortFactory mFactory;
    std::map<u32, std::unique_ptr<EthPort>> mPorts;
    std::vector<Rec> mLinks;
    std::map<u32, u16> mSeams;        //< chip -> link-mask word
    std::vector<Decl> mDecls;
    HubLookup mHubLookup;
  };

} // namespace MFM

#include "EthSeamHost.h"

namespace MFM {

  EthSeamHost & EthSeamHost::get() {
    static EthSeamHost theHost;
    return theHost;
  }

  std::string EthSeamHost::declareSeam(u32 chipA,
                                       u32 dir4,
                                       u32 chipB,
                                       const std::vector<u8> & ethx) {
    if (dir4 > (u32) D4_E) return "bad direction";
    Dir4 d4 = (Dir4) dir4;
    if (chipA == chipB) return "a seam needs two different chips";
    if (ethx.empty() || ethx.size() > EthSeam::MAX_LINKS) {
      return "a seam needs 1 to "
        + std::to_string(EthSeam::MAX_LINKS)
        + " links";
    }
    u32 nibble = 0;
    if (!EthSeam::linkNibble(d4, ethx.data(), ethx.size(), nibble)) {
      return "a link is not in the cabling list for that direction"
        " (EthSeam::SEAM_LINKS_EW / NS), or is listed twice";
    }
    Dir4 back = oppositeDir4(d4);
    std::lock_guard<std::mutex> lock(mMutex);
    u16 & a = mSeams[chipA];
    u16 & b = mSeams[chipB];
    if (((a >> (4u * d4)) & 0xfu) != 0u ||
        ((b >> (4u * back)) & 0xfu) != 0u) {
      return "seam already declared that way";
    }
    a = EthSeam::packSeamMask(d4, nibble, a);
    b = EthSeam::packSeamMask(back, nibble, b);
    mDecls.push_back({ chipA, dir4, chipB });
    return "";
  }

  void EthSeamHost::clearSeams() {
    std::lock_guard<std::mutex> lock(mMutex);
    mSeams.clear();
    mDecls.clear();
  }

  u16 EthSeamHost::seamMaskFor(u32 chip) {
    std::lock_guard<std::mutex> lock(mMutex);
    auto it = mSeams.find(chip);
    return it == mSeams.end() ? (u16) 0u : it->second;
  }

  void EthSeamHost::setPortFactory(PortFactory f) {
    std::lock_guard<std::mutex> g(mMutex);
    mFactory = f;
  }

  EthPort * EthSeamHost::portFor(u32 chip, std::string & err) {
    auto it = mPorts.find(chip);
    if (it != mPorts.end()) return it->second.get();
    if (!mFactory) {
      err = "no EthPort factory installed"
        " (the glue must call setPortFactory)";
      return nullptr;
    }
    std::unique_ptr<EthPort> p = mFactory(chip, err);
    if (!p) {
      if (err.empty()) {
        err = "no device for chip " + std::to_string(chip);
      }
      return nullptr;
    }
    EthPort * raw = p.get();
    mPorts[chip] = std::move(p);
    return raw;
  }

  EthSeamHost::Rec * EthSeamHost::find(u32 a, u32 b, u32 ethx) {
    for (Rec & r : mLinks) {
      if (r.a == a && r.b == b && r.ethx == ethx) return &r;
    }
    return nullptr;
  }

  EthSeamHost::Rec * EthSeamHost::findEnd(u32 chip,
                                          u32 ethx,
                                          EthLinkEnd *& end) {
    for (Rec & r : mLinks) {
      if (r.ethx != ethx) continue;
      if (r.a == chip) {
        end = &r.link->a();
        return &r;
      }
      if (r.b == chip) {
        end = &r.link->b();
        return &r;
      }
    }
    return nullptr;
  }

  void EthSeamHost::closeRec(Rec & r) {
    r.link->tearDown();
  }

  std::string EthSeamHost::bringUpLink(u32 chipA,
                                       u32 chipB,
                                       u32 ethx,
                                       const std::string & imagePath,
                                       const EthChannelSetup & setupA,
                                       const EthChannelSetup & setupB,
                                       bool force,
                                       bool smoke) {
    std::lock_guard<std::mutex> g(mMutex);
    return bringUpLinkLocked(chipA,
                             chipB,
                             ethx,
                             imagePath,
                             setupA,
                             setupB,
                             force,
                             smoke);
  }

  std::string EthSeamHost::bringUpLinkLocked(
      u32 chipA,
      u32 chipB,
      u32 ethx,
      const std::string & imagePath,
      const EthChannelSetup & setupA,
      const EthChannelSetup & setupB,
      bool force,
      bool smoke) {
    if (chipA == chipB) return "a link needs two different chips";
    if (find(chipA, chipB, ethx) || find(chipB, chipA, ethx)) {
      return "link already exists (tear it down first)";
    }
    for (Rec & r : mLinks) {
      bool sameTile = r.ethx == ethx;
      bool usesChip =
        r.a == chipA || r.b == chipA ||
        r.a == chipB || r.b == chipB;
      if (sameTile && usesChip) {
        return "ETH tile " + std::to_string(ethx)
          + " of one of these chips is already in a link";
      }
    }
    std::vector<u32> image;
    std::string err;
    if (!ethLoadImageFile(imagePath, image, err)) return err;
    EthPort * pa = portFor(chipA, err);
    if (!pa) return err;
    EthPort * pb = portFor(chipB, err);
    if (!pb) return err;

    Rec rec;
    rec.a = chipA;
    rec.b = chipB;
    rec.ethx = ethx;
    rec.link.reset(new EthLink(*pa, chipA, *pb, chipB, ethx));
    if (!rec.link->bringUp(image, setupA, setupB, force, smoke)) {
      // bringUp already tore down what it touched
      std::string why = rec.link->lastError();
      return why.empty() ? "bring-up failed" : why;
    }
    for (const EthChannel & ch : setupA.channels) {
      rec.chans.push_back(ch.channel);
    }
    mLinks.push_back(std::move(rec));
    return "";
  }

  std::string EthSeamHost::tearDownLink(u32 chipA,
                                        u32 chipB,
                                        u32 ethx) {
    std::lock_guard<std::mutex> g(mMutex);
    return tearDownLinkLocked(chipA, chipB, ethx);
  }

  std::string EthSeamHost::tearDownLinkLocked(u32 chipA,
                                              u32 chipB,
                                              u32 ethx) {
    for (size_t i = 0; i < mLinks.size(); ++i) {
      Rec & r = mLinks[i];
      bool forward = r.a == chipA && r.b == chipB;
      bool backward = r.a == chipB && r.b == chipA;
      if (r.ethx == ethx && (forward || backward)) {
        closeRec(r);
        mLinks.erase(mLinks.begin() + (long) i);
        return "";
      }
    }
    return "no such link";
  }

  void EthSeamHost::setHubLookup(HubLookup f) {
    std::lock_guard<std::mutex> g(mMutex);
    mHubLookup = f;
  }

  EthSeam::SeamTable EthSeamHost::tableLocked(u32 chip) {
    EthSeam::SeamTable t;
    auto it = mSeams.find(chip);
    u16 mask = it == mSeams.end() ? (u16) 0u : it->second;
    EthSeam::decodeSeamMask(mask, t);
    return t;
  }

  std::string EthSeamHost::planLink(u32 chipA,
                                    u32 chipB,
                                    u32 ethx,
                                    u32 flags,
                                    u32 ringCars,
                                    EthChannelSetup & setupA,
                                    EthChannelSetup & setupB) {
    std::lock_guard<std::mutex> g(mMutex);
    return planLinkLocked(chipA,
                          chipB,
                          ethx,
                          flags,
                          ringCars,
                          setupA,
                          setupB);
  }

  std::string EthSeamHost::planLinkLocked(u32 chipA,
                                          u32 chipB,
                                          u32 ethx,
                                          u32 flags,
                                          u32 ringCars,
                                          EthChannelSetup & setupA,
                                          EthChannelSetup & setupB) {
    if (!mHubLookup) {
      return "no hub lookup installed"
        " (the glue must call setHubLookup)";
    }
    // Find the declaration, in either order, and orient it so that
    // 'a' is the chip that declared the direction.
    const Decl * decl = nullptr;
    u32 a = chipA;
    u32 b = chipB;
    for (const Decl & d : mDecls) {
      if (d.chipA == chipA && d.chipB == chipB) {
        decl = &d;
      } else if (d.chipA == chipB && d.chipB == chipA) {
        decl = &d;
        a = chipB;
        b = chipA;
      }
    }
    if (!decl) return "no seam declared between those chips";

    Dir4 dirAB = (Dir4) decl->dir4;
    Dir4 dirBA = oppositeDir4(dirAB);
    EthSeam::SeamTable ta = tableLocked(a);
    EthSeam::SeamTable tb = tableLocked(b);
    u32 links = ta.n[dirAB];
    u32 link = links;
    for (u32 i = 0; i < links; ++i) {
      if (ta.ethX[dirAB][i] == ethx) link = i;
    }
    if (link == links) {
      return "ETH tile " + std::to_string(ethx)
        + " is not a declared link of that seam";
    }

    // Both ends get the same channel set, each describing where ITS
    // incoming cars land: the receiving hub is found by asking the
    // plan "as sent from the other card".
    EthChannelSetup sa;
    EthChannelSetup sb;
    sa.flags = (u8) flags;
    sb.flags = (u8) flags;
    sa.ringCars = (u8) ringCars;
    sb.ringCars = (u8) ringCars;

    const EthSeam::CellShape & cell = EthSeam::CELL_C2X2;
    u32 E = EthSeam::edgeHubCount(dirAB, cell);
    u32 nch = EthSeam::maxChannelsPerLink(
        EthSeam::channelsTwoCards(E), links);
    for (u32 c = 0; c < nch; ++c) {
      EthSeam::RecvDir4 atB;
      EthSeam::RecvDir4 atA;
      bool toB = EthSeam::recvDir4(ta, dirAB, link, c, cell, atB);
      bool toA = EthSeam::recvDir4(tb, dirBA, link, c, cell, atA);
      if (toB != toA) return "internal: seam ends disagree";
      if (!toB) continue;

      std::string err;
      SeamHub hubA;
      SeamHub hubB;
      if (!mHubLookup(a, atA.recvCT6x, atA.recvCT6y, hubA, err)) {
        return err;
      }
      if (!mHubLookup(b, atB.recvCT6x, atB.recvCT6y, hubB, err)) {
        return err;
      }
      EthChannel ca;
      ca.channel = (u8) c;
      ca.legs = EthSeam::LEG_OUT | EthSeam::LEG_IN_NOC;
      ca.t6x = hubA.noc0x;
      ca.t6y = hubA.noc0y;
      ca.t6Base = hubA.ihubBase + atA.recvDir8 * EthSeam::RING_BYTES;
      EthChannel cb = ca;
      cb.t6x = hubB.noc0x;
      cb.t6y = hubB.noc0y;
      cb.t6Base = hubB.ihubBase + atB.recvDir8 * EthSeam::RING_BYTES;
      sa.channels.push_back(ca);
      sb.channels.push_back(cb);
    }
    if (sa.channels.empty()) return "no Dir4 channel on that link";

    // Hand back in the caller's order.
    if (a == chipA) {
      setupA = sa;
      setupB = sb;
    } else {
      setupA = sb;
      setupB = sa;
    }
    return "";
  }

  std::string EthSeamHost::bringUpSeams(const std::string & imagePath,
                                        u32 flags,
                                        u32 ringCars,
                                        bool force,
                                        bool smoke) {
    std::lock_guard<std::mutex> g(mMutex);
    if (mDecls.empty()) return "no seams declared";
    struct Up {
      u32 a;
      u32 b;
      u32 ethx;
    };
    std::vector<Up> ups;
    std::string err;
    for (const Decl & d : mDecls) {
      EthSeam::SeamTable ta = tableLocked(d.chipA);
      Dir4 dir = (Dir4) d.dir4;
      for (u32 i = 0; i < ta.n[dir] && err.empty(); ++i) {
        u32 ethx = ta.ethX[dir][i];
        EthChannelSetup sa;
        EthChannelSetup sb;
        err = planLinkLocked(d.chipA,
                             d.chipB,
                             ethx,
                             flags,
                             ringCars,
                             sa,
                             sb);
        if (!err.empty()) break;
        err = bringUpLinkLocked(d.chipA,
                                d.chipB,
                                ethx,
                                imagePath,
                                sa,
                                sb,
                                force,
                                smoke);
        if (err.empty()) ups.push_back({ d.chipA, d.chipB, ethx });
      }
      if (!err.empty()) break;
    }
    if (!err.empty()) {
      for (const Up & u : ups) tearDownLinkLocked(u.a, u.b, u.ethx);
    }
    return err;
  }

  void EthSeamHost::tearDownAll() {
    std::lock_guard<std::mutex> g(mMutex);
    for (Rec & r : mLinks) closeRec(r);
    mLinks.clear();
  }

  void EthSeamHost::chipClosing(u32 chip) {
    std::lock_guard<std::mutex> g(mMutex);
    for (size_t i = 0; i < mLinks.size();) {
      if (mLinks[i].a == chip || mLinks[i].b == chip) {
        closeRec(mLinks[i]);
        mLinks.erase(mLinks.begin() + (long) i);
      } else {
        ++i;
      }
    }
    // frees the chip's TLB windows while its fd is still open
    mPorts.erase(chip);
  }

  std::string EthSeamHost::status() {
    std::lock_guard<std::mutex> g(mMutex);
    if (mLinks.empty()) return "no eth-seam links";
    std::string s;
    for (Rec & r : mLinks) {
      s += "link bh" + std::to_string(r.a)
        + " <-> bh" + std::to_string(r.b)
        + " eth" + std::to_string(r.ethx)
        + (r.link->isUp() ? " UP" : " down")
        + "\n";
    }
    return s;
  }

  std::vector<u32> EthSeamHost::channelsOf(u32 chip, u32 ethx) {
    std::lock_guard<std::mutex> g(mMutex);
    EthLinkEnd * end = nullptr;
    Rec * r = findEnd(chip, ethx, end);
    return r ? r->chans : std::vector<u32>();
  }

  bool EthSeamHost::health(u32 chip,
                           u32 ethx,
                           EthHealth & h,
                           std::string & err) {
    std::lock_guard<std::mutex> g(mMutex);
    EthLinkEnd * end = nullptr;
    if (!findEnd(chip, ethx, end)) {
      err = "no link on that chip/tile";
      return false;
    }
    h = end->health();
    return true;
  }

  bool EthSeamHost::channelStat(u32 chip,
                                u32 ethx,
                                u32 channel,
                                u32 wordIndex,
                                u32 & value,
                                std::string & err) {
    std::lock_guard<std::mutex> g(mMutex);
    EthLinkEnd * end = nullptr;
    if (!findEnd(chip, ethx, end)) {
      err = "no link on that chip/tile";
      return false;
    }
    if (channel >= EthSeam::NCH_MAX || wordIndex >= 24u) {
      err = "bad channel or word index";
      return false;
    }
    value = end->channelStat(channel, wordIndex);
    return true;
  }

} // namespace MFM

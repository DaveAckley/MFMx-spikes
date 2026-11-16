#include "EthSeamHost.h"

namespace MFM {

  EthSeamHost & EthSeamHost::get() {
    static EthSeamHost theHost;
    return theHost;
  }

  void EthSeamHost::setPortFactory(PortFactory f) {
    std::lock_guard<std::mutex> g(mMutex);
    mFactory = f;
  }

  EthPort * EthSeamHost::portFor(u32 chip, std::string & err) {
    auto it = mPorts.find(chip);
    if (it != mPorts.end()) return it->second.get();
    if (!mFactory) { err = "no EthPort factory installed (the glue must call setPortFactory)"; return nullptr; }
    std::unique_ptr<EthPort> p = mFactory(chip, err);
    if (!p) { if (err.empty()) err = "no device for chip " + std::to_string(chip); return nullptr; }
    EthPort * raw = p.get();
    mPorts[chip] = std::move(p);
    return raw;
  }

  EthSeamHost::Rec * EthSeamHost::find(u32 a, u32 b, u32 ethx) {
    for (Rec & r : mLinks) if (r.a == a && r.b == b && r.ethx == ethx) return &r;
    return nullptr;
  }

  EthSeamHost::Rec * EthSeamHost::findEnd(u32 chip, u32 ethx, EthLinkEnd *& end) {
    for (Rec & r : mLinks) {
      if (r.ethx != ethx) continue;
      if (r.a == chip) { end = &r.link->a(); return &r; }
      if (r.b == chip) { end = &r.link->b(); return &r; }
    }
    return nullptr;
  }

  void EthSeamHost::closeRec(Rec & r) {
    r.link->tearDown();
  }

  std::string EthSeamHost::bringUpLink(u32 chipA, u32 chipB, u32 ethx, const std::string & imagePath,
                                       const EthChannelSetup & setupA, const EthChannelSetup & setupB, bool force, bool smoke) {
    std::lock_guard<std::mutex> g(mMutex);
    if (chipA == chipB) return "a link needs two different chips";
    if (find(chipA, chipB, ethx) || find(chipB, chipA, ethx)) return "link already exists (tear it down first)";
    for (Rec & r : mLinks)
      if (r.ethx == ethx && (r.a == chipA || r.b == chipA || r.a == chipB || r.b == chipB)) return "ETH tile " + std::to_string(ethx) + " of one of these chips is already in a link";
    std::vector<u32> image;
    std::string err;
    if (!ethLoadImageFile(imagePath, image, err)) return err;
    EthPort * pa = portFor(chipA, err);
    if (!pa) return err;
    EthPort * pb = portFor(chipB, err);
    if (!pb) return err;
    Rec rec;
    rec.a = chipA; rec.b = chipB; rec.ethx = ethx;
    rec.link.reset(new EthLink(*pa, chipA, *pb, chipB, ethx));
    if (!rec.link->bringUp(image, setupA, setupB, force, smoke)) {
      std::string why = rec.link->lastError();     // bringUp already tore down what it touched
      return why.empty() ? "bring-up failed" : why;
    }
    mLinks.push_back(std::move(rec));
    return "";
  }

  std::string EthSeamHost::tearDownLink(u32 chipA, u32 chipB, u32 ethx) {
    std::lock_guard<std::mutex> g(mMutex);
    for (size_t i = 0; i < mLinks.size(); ++i) {
      Rec & r = mLinks[i];
      if (r.ethx == ethx && ((r.a == chipA && r.b == chipB) || (r.a == chipB && r.b == chipA))) {
        closeRec(r);
        mLinks.erase(mLinks.begin() + (long) i);
        return "";
      }
    }
    return "no such link";
  }

  void EthSeamHost::tearDownAll() {
    std::lock_guard<std::mutex> g(mMutex);
    for (Rec & r : mLinks) closeRec(r);
    mLinks.clear();
  }

  void EthSeamHost::chipClosing(u32 chip) {
    std::lock_guard<std::mutex> g(mMutex);
    for (size_t i = 0; i < mLinks.size();) {
      if (mLinks[i].a == chip || mLinks[i].b == chip) { closeRec(mLinks[i]); mLinks.erase(mLinks.begin() + (long) i); }
      else ++i;
    }
    mPorts.erase(chip);                              // frees the chip's TLB windows while its fd is still open
  }

  std::string EthSeamHost::status() {
    std::lock_guard<std::mutex> g(mMutex);
    if (mLinks.empty()) return "no eth-seam links";
    std::string s;
    for (Rec & r : mLinks) {
      s += "link bh" + std::to_string(r.a) + " <-> bh" + std::to_string(r.b) + " eth" + std::to_string(r.ethx) + (r.link->isUp() ? " UP" : " down") + "\n";
    }
    return s;
  }

  bool EthSeamHost::health(u32 chip, u32 ethx, EthHealth & h, std::string & err) {
    std::lock_guard<std::mutex> g(mMutex);
    EthLinkEnd * end = nullptr;
    if (!findEnd(chip, ethx, end)) { err = "no link on that chip/tile"; return false; }
    h = end->health();
    return true;
  }

  bool EthSeamHost::channelStat(u32 chip, u32 ethx, u32 channel, u32 wordIndex, u32 & value, std::string & err) {
    std::lock_guard<std::mutex> g(mMutex);
    EthLinkEnd * end = nullptr;
    if (!findEnd(chip, ethx, end)) { err = "no link on that chip/tile"; return false; }
    if (channel >= EthSeam::NCH_MAX || wordIndex >= 24u) { err = "bad channel or word index"; return false; }
    value = end->channelStat(channel, wordIndex);
    return true;
  }

} // namespace MFM

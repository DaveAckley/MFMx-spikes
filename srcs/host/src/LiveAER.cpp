#include "LiveAER.h"
#include <cstdio>

namespace MFM {

  LiveAER & LiveAER::get() {
    static LiveAER theLiveAER;
    return theLiveAER;
  }

  bool LiveAER::parseEv(const std::string & msg, uint64_t & events) {
    if (msg.compare(0, 4, ".ev:") != 0) return false;
    unsigned long long c = 0, r = 0, e = 0;
    if (sscanf(msg.c_str() + 4, "%llx:%llx:%llx", &c, &r, &e) != 3)
      return false;
    events = (uint64_t) c + (uint64_t) e;
    return true;
  }

  void LiveAER::noteMark(uint8_t chip, uint8_t x, uint8_t y,
                         uint64_t hubMs, const std::string & msg) {
    if (msg.size() < 4 || msg[0] != '.') return; // the cheap common case
    if (msg.compare(0, 4, ".as:") == 0) {
      std::lock_guard<std::mutex> lock(mLock);
      mSeeded = true;
      return;
    }
    uint64_t events;
    if (!parseEv(msg, events)) return;
    uint32_t key = ((uint32_t) chip << 16) | ((uint32_t) x << 8) | y;
    std::lock_guard<std::mutex> lock(mLock);
    HubEvents & h = mHubs[key];
    if (h.mKey != 0u && hubMs == h.mHubMs) return; // repeated mark
    h.mKey = key | 0x80000000u; // never 0, even for chip 0 at (0,0)
    h.mEvents = events;
    h.mHubMs = hubMs;
  }

  std::vector<LiveAER::HubEvents> LiveAER::snapshot(bool & seeded) {
    std::lock_guard<std::mutex> lock(mLock);
    seeded = mSeeded;
    std::vector<HubEvents> out;
    out.reserve(mHubs.size());
    for (const auto & kv : mHubs) out.push_back(kv.second);
    return out;
  }

  void LiveAER::clear() {
    std::lock_guard<std::mutex> lock(mLock);
    mHubs.clear();
    mSeeded = false;
  }
}

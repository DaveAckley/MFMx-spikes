#include "LiveAER.h"
#include <cstdio>

namespace MFM {

  LiveAER & LiveAER::get() {
    static LiveAER theLiveAER;
    return theLiveAER;
  }

  bool LiveAER::parseEv(const std::string & msg, u64 & events) {
    if (msg.compare(0, 4, ".ev:") != 0) return false;
    unsigned long long c = 0, r = 0, e = 0;
    if (sscanf(msg.c_str() + 4, "%llx:%llx:%llx", &c, &r, &e) != 3)
      return false;
    events = (u64) c + (u64) e;
    return true;
  }

  void LiveAER::noteMark(u8 chip, u8 x, u8 y,
                         u64 hubMs, const std::string & msg) {
    if (msg.size() < 4 || msg[0] != '.') return; // the cheap common case
    if (msg.compare(0, 4, ".as:") == 0) {
      std::lock_guard<std::mutex> lock(mLock);
      mSeeded = true;
      return;
    }
    u64 events;
    if (!parseEv(msg, events)) return;
    u32 key = ((u32) chip << 16) | ((u32) x << 8) | y;
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

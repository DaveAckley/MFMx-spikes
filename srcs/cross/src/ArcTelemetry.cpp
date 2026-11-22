#include "ArcTelemetry.h"
#include "NRIUtils.h"
#include "CrossUtils.h"   // for memoryFence
#include "FastLocal.h"    // for FAll (MFM_API_ASSERT_ON_HART)
#include "Debug.h"

namespace MFM {

  ArcClocks theArcClocks;

  namespace {
    // ARC tile (NoC0 raw (8,0); the one column NoC1 maps to itself).
    constexpr u32 ARC_SCRATCH_BASE = 0x80030400u;
    constexpr u32 SC_BOOT_STATUS = 2u;
    constexpr u32 SC_TELEMETRY_DATA = 12u;
    constexpr u32 SC_TELEMETRY_PTR = 13u;
    constexpr u32 TAG_AICLK = 14u;
    constexpr u32 TAG_AXICLK = 15u;
    constexpr u32 TAG_ARCCLK = 16u;
    constexpr u32 TAG_AICLK_LIMIT_MAX = 63u;
    constexpr u32 MAX_ENTRIES = 128u;
    constexpr u32 CHUNK_WORDS = 12u;
    constexpr u32 SPIN_MAX = 4'000'000u;

    // The NIU delivers read data at (return address & ~63) + (source
    // & 63): 64-aligned buffer, offset by the source address mod 64;
    // 63 + 48 bytes fit in 128.
    alignas(64) volatile u32 sArcBuf[32];

    inline u32 arcScratch(u32 n) { return ARC_SCRATCH_BASE + 4u * n; }

    // Read wordcount (1..12) words at addr of the ARC tile over NoC0
    // (NRI3, our hart NC).  False on a timeout: never fatal.
    bool arcRead(U8C usnoc0, U8C arcnoc0, u32 addr,
                 u32 wordcount, u32 * dest) {
      if (wordcount == 0u || wordcount > CHUNK_WORDS) return false;
      const u32 useNoC = 0u;
      u32 spin = 0u;
      while (readNIUReqsOutstanding(useNoC,
                                    NRI3::NRI3_BLOCKING_TRANSACTION_ID) > 0)
        if (++spin > SPIN_MAX) return false;
      while (NRI3::isNRIBusy(useNoC))
        if (++spin > SPIN_MAX) return false;

      const u32 buf = (u32) &sArcBuf[0] + (addr & 63u);
      NRI3::writeNRIAddress(useNoC, NRI_NOC_TARG_ADDR_LO, addr);
      NRI3::writeNRIAddress(useNoC, NRI_NOC_TARG_ADDR_MID, 0);
      NRI3::writeNRIAddress(useNoC, NRI_NOC_TARG_ADDR_HI,
                            U8C::makeNoCNodeIdFromNoCCoord(arcnoc0));
      NRI3::writeNRIAddress(useNoC, NRI_NOC_RET_ADDR_LO, buf);
      NRI3::writeNRIAddress(useNoC, NRI_NOC_RET_ADDR_MID, 0);
      NRI3::writeNRIAddress(useNoC, NRI_NOC_RET_ADDR_HI,
                            U8C::makeNoCNodeIdFromNoCCoord(usnoc0));
      NRI3::writeNRIAddress(useNoC, NRI_NOC_PACKET_TAG,
                            NRI3::NRI3_BLOCKING_TRANSACTION_ID << 10);
      NRI3::writeNRIAddress(useNoC, NRI_NOC_CTRL, 0u);   // NOC_CMD_RD
      NRI3::writeNRIAddress(useNoC, NRI_NOC_AT_LEN_BE, wordcount << 2u);
      NRI3::writeNRIAddress(useNoC, NRI_NOC_AT_LEN_BE_1, 0u);
      NRI3::writeNRIAddress(useNoC, NRI_NOC_CMD_CTRL, 1u);
      NRI3::readNRIAddress(useNoC, NRI_NOC_CMD_CTRL);    // ordering read
      while (readNIUReqsOutstanding(useNoC,
                                    NRI3::NRI3_BLOCKING_TRANSACTION_ID) > 0)
        if (++spin > SPIN_MAX) return false;
      memoryFence();            // flush L0 before reading the data
      const volatile u32 * data = (const volatile u32 *) buf;
      for (u32 i = 0u; i < wordcount; ++i) dest[i] = data[i];
      ++theArcClocks.mReads;
      return true;
    }

    inline bool plausible(u32 p) {
      return p != 0u && p != 0xFFFFFFFFu && (p & 3u) == 0u;
    }
  }

  void readArcClocksNC(U8C usnoc0) {
    MFM_API_ASSERT_ON_HART(HARTNUM_NC);
    ArcClocks & ac = theArcClocks;
    ac.mStatus = 1u;
    ac.mAiclk = ac.mAxiclk = ac.mArcclk = ac.mLimit = 0u;
    ac.mReads = 0u;
    const U8C arc(8u, 0u);
    u32 w[CHUNK_WORDS];
    u32 tdata = 0u, tptr = 0u;

    if (!arcRead(usnoc0, arc, arcScratch(SC_BOOT_STATUS), 1u, w)) {
      ac.mStatus = 3u; return;
    }
    if (!(w[0] & 1u) || w[0] == 0xFFFFFFFFu) { // not ready for messages
      ac.mStatus = 2u; return;
    }
    if (!arcRead(usnoc0, arc, arcScratch(SC_TELEMETRY_DATA), 1u, w)) {
      ac.mStatus = 3u; return;
    }
    tdata = w[0];
    if (!arcRead(usnoc0, arc, arcScratch(SC_TELEMETRY_PTR), 1u, w)) {
      ac.mStatus = 3u; return;
    }
    tptr = w[0];
    if (!plausible(tdata) || !plausible(tptr)) { ac.mStatus = 2u; return; }

    if (!arcRead(usnoc0, arc, tptr, 2u, w)) { ac.mStatus = 3u; return; }
    const u32 count = w[1];
    if (count == 0u || count > MAX_ENTRIES) { ac.mStatus = 4u; return; }

    u32 off[4] = { 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu };
    for (u32 e = 0u; e < count; e += CHUNK_WORDS) {
      u32 n = count - e;
      if (n > CHUNK_WORDS) n = CHUNK_WORDS;
      if (!arcRead(usnoc0, arc, tptr + 8u + 4u * e, n, w)) {
        ac.mStatus = 3u; return;
      }
      for (u32 k = 0u; k < n; ++k) {
        const u32 id = w[k] & 0xffffu;
        if (id == TAG_AICLK) off[0] = w[k] >> 16;
        else if (id == TAG_AXICLK) off[1] = w[k] >> 16;
        else if (id == TAG_ARCCLK) off[2] = w[k] >> 16;
        else if (id == TAG_AICLK_LIMIT_MAX) off[3] = w[k] >> 16;
      }
    }
    if (off[0] == 0xFFFFFFFFu) { ac.mStatus = 5u; return; }
    u32 * out[4] = { &ac.mAiclk, &ac.mAxiclk, &ac.mArcclk, &ac.mLimit };
    for (u32 i = 0u; i < 4u; ++i) {
      if (off[i] == 0xFFFFFFFFu) continue;
      if (!arcRead(usnoc0, arc, tdata + 4u * off[i], 1u, w)) {
        ac.mStatus = 3u; return;
      }
      *out[i] = w[0];
    }
    ac.mStatus = 0u;
  }

  const char * formatArcClocks(char * b, u32 siz, const ArcClocks & ac) {
    static const char HEX[] = "0123456789abcdef";
    u32 n = 0u;
    const u32 vals[5] = { ac.mStatus, ac.mAiclk & 0xffffu,
                          ac.mAxiclk & 0xffffu, ac.mArcclk & 0xffffu,
                          ac.mLimit & 0xffffu };
    for (u32 v = 0u; v < 5u; ++v) {
      u32 digits = 1u;
      for (u32 t = vals[v] >> 4; t; t >>= 4) ++digits;
      if (n + digits + 2u >= siz) break;
      for (u32 d = digits; d > 0u; --d)
        b[n++] = HEX[(vals[v] >> (4u * (d - 1u))) & 15u];
      b[n++] = (v < 4u) ? ':' : '.';
    }
    b[n] = '\0';
    return b;
  }
}

// Host stress test of lzmfmx: long streams of report-like records through the
// real encoder and decoder, with the source "blocked" (-2) and the sink
// refusing bytes at random, as on the hub.  Any mismatch is a compressor or
// decoder bug (not the NoC, not the host copy).  usage: t SEED STREAMS MB
#include "lzmfmx.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstdint>
using namespace MFM;

static uint64_t rs;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t) (rs >> 11); }

struct Src { const std::vector<u8> * d; size_t pos = 0; bool noise; };
static s32 srcFn(bool canread, void * c) {
  Src & s = *(Src *) c;
  if (canread) return s.pos >= s.d->size() ? -1 : 0;
  if (s.pos >= s.d->size()) return -1;
  if (s.noise && (rnd() & 15) == 0) return -2;      // blocked now
  return (*s.d)[s.pos++];
}
struct Snk { std::vector<u8> * o; bool noise; };
static bool sinkFn(const u8 b, void * c) {
  Snk & s = *(Snk *) c;
  if (s.noise && (rnd() & 31) == 0) return false;   // car full right now
  s.o->push_back(b); return true;
}

// Report-like: 12 B atom (few distinct patterns, some random), coord, spin x2.
static std::vector<u8> records(size_t bytes, uint32_t spin0) {
  std::vector<u8> v; v.reserve(bytes + 20);
  uint32_t spin = spin0;
  while (v.size() < bytes) {
    uint32_t mode = rnd() % 8;
    u8 a[12];
    for (int k = 0; k < 12; ++k) {
      if (mode < 4) a[k] = k == 0 ? 4 : k == 1 ? 0x38 : (k & 3) ? 0 : (u8) (rnd() % 9);
      else if (mode < 7) a[k] = (u8) (rnd() % 5);
      else a[k] = (u8) rnd();
    }
    for (int k = 0; k < 12; ++k) v.push_back(a[k]);
    uint32_t x = rnd() % 150, y = rnd() % 120;
    v.push_back(x); v.push_back(0); v.push_back(y); v.push_back(0);
    v.push_back(spin & 0xff); v.push_back((spin >> 8) & 0xff);
    v.push_back(spin & 0xff); v.push_back((spin >> 8) & 0xff);
    ++spin;
  }
  return v;
}

static lzmfmx enc, dec;
int main(int argc, char ** argv) {
  uint64_t seed = argc > 1 ? atoll(argv[1]) : 1;
  int streams = argc > 2 ? atoi(argv[2]) : 4;
  size_t mb = argc > 3 ? atoi(argv[3]) : 16;
  rs = seed * 0x9E3779B97F4A7C15ull + 12345;
  for (int i = 0; i < 20; ++i) rnd();
  size_t total = 0;
  for (int s = 0; s < streams; ++s) {
    std::vector<u8> in = records(mb << 20, (uint32_t) rnd());
    std::vector<u8> comp; comp.reserve(in.size());
    Src es{&in, 0, true}; Snk ek{&comp, true};
    enc.init(srcFn, &es, sinkFn, &ek);
    enc.encode();
    std::vector<u8> out; out.reserve(in.size());
    Src ds{&comp, 0, true}; Snk dk{&out, false};
    dec.init(srcFn, &ds, sinkFn, &dk);
    dec.decode();
    total += in.size();
    if (out != in) {
      size_t i = 0; while (i < in.size() && i < out.size() && in[i] == out[i]) ++i;
      std::printf("MISMATCH seed %llu stream %d: in %zu out %zu, first diff at %zu (record %zu +%zu), comp %zu\n",
                  (unsigned long long) seed, s, in.size(), out.size(), i, i / 20, i % 20, comp.size());
      return 1;
    }
    std::printf("seed %llu stream %d ok (%zu B -> %zu B)\n", (unsigned long long) seed, s, in.size(), comp.size());
    std::fflush(stdout);
  }
  std::printf("PASS %zu MB\n", total >> 20);
  return 0;
}

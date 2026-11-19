// Host test of lzmfmx restartability: encode to EOF, reset(), encode again;
// a fresh decoder must read each stream.  Also: an EOF that arrives while the
// encoder is blocked waiting for input (the hub's drain), and the proof that
// WITHOUT reset() the second stream is not decodable (why reset() exists).
#include "lzmfmx.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <unistd.h>
#include <sys/wait.h>

using namespace MFM;
static int fails = 0;
static void check(const char * name, bool ok) {
  std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
  if (!ok) ++fails;
}

struct Src {                    // bytes, then EOF (or, with blockFirst, -2 a few times first)
  const std::vector<u8> * d; size_t pos = 0; int blockFirst = 0;
  bool endOnEmpty = true;
};
static s32 srcFn(bool canread, void * c) {
  Src & s = *(Src *) c;
  if (canread) return (s.pos >= s.d->size() && s.endOnEmpty && s.blockFirst <= 0) ? -1 : 0;
  if (s.pos < s.d->size()) return (*s.d)[s.pos++];
  if (s.blockFirst > 0) { --s.blockFirst; return -2; }
  return s.endOnEmpty ? -1 : -2;
}
static bool sinkFn(const u8 b, void * c) { ((std::vector<u8> *) c)->push_back(b); return true; }

static std::vector<u8> records(unsigned n, unsigned spin0) { // 20-byte records, report-like
  std::vector<u8> v;
  for (unsigned i = 0; i < n; ++i) {
    unsigned spin = spin0 + i;
    for (int k = 0; k < 12; ++k) v.push_back((u8) ((i * 7 + k * 13) & ((k & 1) ? 0x0f : 0xff)));
    v.push_back((u8) (i % 150)); v.push_back(0); v.push_back((u8) (i % 120)); v.push_back(0);
    v.push_back(spin & 0xff); v.push_back(spin >> 8); v.push_back(spin & 0xff); v.push_back(spin >> 8);
  }
  return v;
}

static lzmfmx enc, dec;         // big: keep off the stack

// One persistent source/sink so that reset() (which keeps them) is exercised.
static Src gsrc; static std::vector<u8> gout; static std::vector<u8> gin;
static std::vector<u8> encodeStream(const std::vector<u8> & in, bool first, bool doReset) {
  gin = in; gsrc.d = &gin; gsrc.pos = 0; gsrc.blockFirst = 0; gsrc.endOnEmpty = true; gout.clear();
  if (first) enc.init(srcFn, &gsrc, sinkFn, &gout);
  else if (doReset) enc.reset();
  enc.encode();
  return gout;
}
static std::vector<u8> decodeStream(const std::vector<u8> & comp) {
  std::vector<u8> out; Src s{&comp};
  dec.init(srcFn, &s, sinkFn, &out);
  dec.decode();
  return out;
}

int main() {
  std::vector<u8> a = records(300, 100), b = records(250, 400), c = records(1, 7);
  check("round trip, first stream", decodeStream(encodeStream(a, true, false)) == a);
  check("restart: reset() then a second stream decodes", decodeStream(encodeStream(b, false, true)) == b);
  check("restart again, and a one-record stream", decodeStream(encodeStream(c, false, true)) == c);
  check("empty stream is legal", decodeStream(encodeStream({}, false, true)).empty());
  std::vector<u8> d = records(260, 900);
  // Without reset() the next stream is wrong or the encoder never returns: run it in a
  // child with an alarm so a hang is a result, not a hung test.
  {
    pid_t pid = fork();
    if (pid == 0) {
      alarm(5);
      encodeStream(a, true, false);       // dirty the ring and trees
      std::vector<u8> nore = decodeStream(encodeStream(d, false, false));
      _exit(nore == d ? 0 : 3);           // 0 = (unexpectedly) fine, 3 = garbage
    }
    int st = 0; waitpid(pid, &st, 0);
    bool bad = WIFSIGNALED(st) || (WIFEXITED(st) && WEXITSTATUS(st) == 3);
    std::printf("      (no reset: %s)\n", WIFSIGNALED(st) ? "encoder hung (alarm)" :
                WEXITSTATUS(st) == 3 ? "garbage stream" : "decoded fine");
    check("WITHOUT reset() the next stream is wrong or hangs (why reset exists)", bad);
  }

  // EOF while blocked: the encoder waits (-2) for input, then the source ends the stream.
  std::vector<u8> e = records(120, 5);
  gin = e; gsrc.d = &gin; gsrc.pos = 0; gsrc.blockFirst = 0; gsrc.endOnEmpty = true; gout.clear();
  enc.init(srcFn, &gsrc, sinkFn, &gout);
  gsrc.blockFirst = 0;
  struct Blocker { static s32 fn(bool canread, void * c) {
    Src & s = *(Src *) c;
    if (canread) return 0;                                   // never EOF to the test
    if (s.pos < s.d->size()) return (*s.d)[s.pos++];
    if (s.blockFirst++ < 1000) return -2;                    // blocked for a while ...
    return -1;                                               // ... then EOF arrives in the READ
  } };
  gout.clear(); enc.init(Blocker::fn, &gsrc, sinkFn, &gout);
  gsrc.pos = 0; gsrc.blockFirst = 0;
  enc.encode();
  check("EOF arriving during a blocked read ends the stream cleanly", decodeStream(gout) == e);
  std::printf(fails ? "FAILED %d\n" : "all ok\n", fails);
  return fails ? 1 : 0;
}

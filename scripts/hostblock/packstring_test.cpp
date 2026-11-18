// Host-side test of HostBlock::packString keeping the LATEST marks.
// Build and run (from the repo root):
//   g++ -std=c++17 -DBUILD_HOST -Isrcs/shared/include -Isrcs/host/include \
//       scripts/hostblock/packstring_test.cpp -o /tmp/psl && /tmp/psl
extern "C" void DieHereNow(signed, const char*, unsigned, const char*) { __builtin_trap(); }
#include <cstdio>
#include <cstring>
#include <string>
#include "HostBlock.h"
using namespace MFM;
int main() {
  static HostBlock hb;
  memset(&hb, 0, sizeof(hb));
  hb.resetLog();
  int fails = 0;
  const int N = 3000;                       // far more than the ring holds
  for (int i = 0; i < N; ++i) {
    char b[64];
    snprintf(b, sizeof(b), "{9:%d %d 0 1,2 hb .t:%d} \n", i, i, i);
    hb.packString(b);
  }
  std::string out;
  // host reads u16 little endian: low byte first
  while (!hb.mLogBuffer.isEmpty()) {
    u16 w; hb.mLogBuffer.remove(w);
    out += (char)(w & 0xff); out += (char)(w >> 8);
  }
  printf("bytes %zu\n", out.size());
  bool headOk = out[0] == '{';
  // every '{' must start a complete mark; last mark must be i = N-1
  size_t last = out.rfind("hb .t:");
  int lastI = atoi(out.c_str() + last + 6);
  size_t torn = 0, marks = 0, pos = 0;
  while ((pos = out.find('{', pos)) != std::string::npos) {
    size_t e = out.find("} \n", pos);
    if (e == std::string::npos) { ++torn; break; }
    ++marks; pos = e + 3;
  }
  printf("head '{' %d, marks %zu, torn %zu, last i %d\n", headOk, marks, torn, lastI);
  if (!headOk || torn || lastI != N - 1 || marks < 100) ++fails;
  // fit without overflow: nothing dropped
  hb.resetLog();
  hb.packString("{1:1 1 0 1,2 hb .a:1} \n");
  hb.packString("{1:1 2 0 1,2 hb .a:2} \n");
  int n = 0; u16 w;
  while (hb.mLogBuffer.remove(w)) ++n;
  printf("small: %d words\n", n);
  if (n != 2 * 12) ++fails;
  printf(fails ? "FAIL\n" : "PASS\n");
  return fails;
}

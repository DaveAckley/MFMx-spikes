/* hosttest2.c -- native harness for t6pp2.c (T6 car ping-pong image).
 * Two "T6 tiles" A (initiator) and B (reflector) run as threads with fake L1s.  A fake E1 sits between them: a NoC write to the E1 OUT ring
 * (node E1NODE, address E1OUT0 + c*OSTRIDE + i*CAR) is delivered, word by word in ASCENDING order (header first, footer last, like the TT-link
 * chunks), into the peer tile's receive slot (c,i).  Checks: both sides keep reflecting (handled counts within NR of each other), zero
 * payload errors with verify-every-car, no NoC failures, and that the cars never stop.
 * Build/run: gcc -O2 -Wall -Wextra -pthread hosttest2.c -o /tmp/t6pp_ht && /tmp/t6pp_ht      (no riscv toolchain needed)
 */
#define HOSTTEST 1
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
typedef unsigned int u32;
__thread char* g_l1; __thread u32 g_nri[0x20];
static __thread char* g_peer;
static const u32 E1NODE = (1u << 6) | 6u, OUT0 = 0x10000u, OSTR = 0x4100u, RCV0 = 0x100000u, RSTR = 0x4100u;
static int g_bad_noc;
#ifndef CAR
#define CAR 0x1040u
#endif
void ht_noc_cmd(void) {
  u32 src = g_nri[0], dst = g_nri[0x0c / 4], node = g_nri[0x14 / 4], n = g_nri[0x20 / 4];
  if (g_nri[0x1c / 4] != 2 || n == 0 || n > 16384 || node != E1NODE || (src % 16) != (dst % 16) || dst < OUT0 || dst >= OUT0 + 8 * OSTR) {
    fprintf(stderr, "BAD NOC cmd node=0x%x dst=0x%x n=%u\n", node, dst, n); g_bad_noc = 1; g_nri[0x40 / 4] = 0; return; }
  u32 c = (dst - OUT0) / OSTR, i = ((dst - OUT0) % OSTR) / CAR;
  volatile u32* s = (volatile u32*)(g_l1 + src); volatile u32* d = (volatile u32*)(g_peer + RCV0 + c * RSTR + i * CAR);
  for (u32 w = 0; w < n / 4; ++w) d[w] = s[w];
  g_nri[0x40 / 4] = 0;
}
#include "t6pp2.c"

#define MEM 0x140000u
#define W32(b, a, v) (*(volatile u32*)((b) + (a)) = (v))
#define R32(b, a) (*(volatile u32*)((b) + (a)))
struct node { char* l1; char* peer; };
static void* run(void* p) { struct node* n = p; g_l1 = n->l1; g_peer = n->peer; pp_run(); return 0; }

static void cfg(char* l1, u32 role, u32 nch, u32 nr, u32 ve, u32 tcms, u32 own) {
  memset(l1, 0, MEM);
  W32(l1, CTLB + 0x10, role); W32(l1, CTLB + 0x14, nch); W32(l1, CTLB + 0x18, nr); W32(l1, CTLB + 0x1c, ve); W32(l1, CTLB + 0x20, tcms);
  W32(l1, CTLB + 0x24, E1NODE); W32(l1, CTLB + 0x28, own); W32(l1, CTLB + 0x2c, OUT0); W32(l1, CTLB + 0x30, OSTR); W32(l1, CTLB + 0x34, RCV0); W32(l1, CTLB + 0x38, RSTR);
}
static u32 stat(char* l1, u32 c, u32 k) { return R32(l1, ST(c, k)); }

int main(void) {
  char* A = calloc(1, MEM); char* B = calloc(1, MEM);
  u32 tcmsl[] = {0, 1, 2, 3, 10, 66}, nrl[] = {1, 2, 4};
  int fails = 0;
  for (int t = 0; t < 6; ++t) for (int k = 0; k < 3; ++k) for (int vv = 0; vv < 2; ++vv) {
    u32 tcms = tcmsl[t], nr = nrl[k], nch = 3, ve = vv ? 1 : 0;
    cfg(A, 0, nch, nr, ve, tcms, (2u << 6) | 1u); cfg(B, 1, nch, nr, ve, tcms, (2u << 6) | 2u);
    g_bad_noc = 0;
    struct node na = {A, B}, nb = {B, A};
    pthread_t ta, tb; pthread_create(&tb, 0, run, &nb); pthread_create(&ta, 0, run, &na);
    struct timespec ts = {0, 30 * 1000000}; nanosleep(&ts, 0);
    u32 h0[3]; for (u32 c = 0; c < nch; ++c) h0[c] = stat(A, c, P_HANDLED);
    nanosleep(&ts, 0);
    int bad = 0;
    for (u32 c = 0; c < nch; ++c) if (stat(A, c, P_HANDLED) <= h0[c]) bad = 1;       /* still making progress */
    W32(A, CTLB + 4, 1); W32(B, CTLB + 4, 1); pthread_join(ta, 0); pthread_join(tb, 0);
    u32 tot = 0;
    for (u32 c = 0; c < nch; ++c) {
      u32 ha = stat(A, c, P_HANDLED), hb = stat(B, c, P_HANDLED);
      tot += ha + hb;
      if ((ha > hb ? ha - hb : hb - ha) > nr + 1) { fprintf(stderr, "ch%u handled A=%u B=%u differ\n", c, ha, hb); bad = 1; }
      if (stat(A, c, P_BAD) || stat(B, c, P_BAD) || stat(A, c, P_NOCFAIL) || stat(B, c, P_NOCFAIL)) { fprintf(stderr, "ch%u bad payload/noc fail\n", c); bad = 1; }
      if (ve && (stat(A, c, P_VCHK) < ha / 2 || stat(B, c, P_VCHK) < hb / 2)) { fprintf(stderr, "ch%u verify count low\n", c); bad = 1; }
    }
    if (g_bad_noc) bad = 1;
    printf("tcms=%2u nr=%u ve=%u handled(total)=%u partialA=%u csr=%08x %s\n", tcms, nr, ve, tot, stat(A, 0, P_PARTIAL), R32(A, CTLB + 0x44), bad ? "FAIL" : "ok");
    fails += bad;
  }
  printf("%s\n", fails ? "SOME TESTS FAILED" : "all ok");
  return fails != 0;
}

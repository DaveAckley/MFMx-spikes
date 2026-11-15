/* hosttest.c -- native harness for e1fwd1.c: node A (FWD) and node B (REFLECT) as threads with fake L1 and fake TXQ2. */
#define HOSTTEST 1
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef unsigned int u32;
__thread char* g_l1; __thread u32 g_txq[0x40];
static __thread char* g_peer;
void ht_txq_cmd(void) {
  u32 src = g_txq[0x14/4], dst = g_txq[0x1c/4], n = g_txq[0x18/4];
  if (g_txq[4/4] != 2 || n == 0 || n > 1456 || (n & 15)) { fprintf(stderr, "BAD TXQ n=%u\n", n); exit(2); }
  volatile u32* s = (volatile u32*)(g_l1 + src); volatile u32* d = (volatile u32*)(g_peer + dst);
  for (u32 i = 0; i < n / 4; ++i) d[i] = s[i];
}
#include "e1fwd1.c"
static char *L1A, *L1B;
#define W32(base,a,v) (*(volatile u32*)((base)+(a)) = (v))
#define R32(base,a) (*(volatile u32*)((base)+(a)))
struct node { char* l1; char* peer; };
static void* run(void* p) { struct node* n = p; g_l1 = n->l1; g_peer = n->peer; e1_run(); return 0; }
static const u32 AOUT = 0x10000, AIN = 0x30000, BIN = 0x20000;
static u32 tcmsl[] = {0,1,2,3,10,66};
static void fillcar(char* base, u32 slot, u32 n, u32 tcms, u32 state, u32 salt) {
  u32 bytes = pkt_bytes(tcms), words = bytes / 4, fi = words - 1, ai = tcms >= 2 ? words - 5 : fi;
  for (u32 w = 1; w < words; ++w) if (w != fi && w != ai) W32(base, slot + 4*w, payload_word(n, w, salt));
  u32 h = mk_hdr(n, tcms, state);
  if (tcms >= 2) W32(base, slot + 4*ai, h);
  W32(base, slot + 4*fi, h); W32(base, slot, h);
}
int main(void) {
  L1A = calloc(1, 0x100000); L1B = calloc(1, 0x100000);
  int fails = 0;
  for (int t = 0; t < 6; ++t) {
    u32 tcms = tcmsl[t];
    memset(L1A, 0, 0x100000); memset(L1B, 0, 0x100000);
    /* A: FWD ch0 out=AOUT in=AIN peerin=BIN; B: REFLECT ch0 in=BIN peerin=AIN */
    W32(L1A, CHC + 0, AOUT); W32(L1A, CHC + 4, AIN); W32(L1A, CHC + 8, BIN);
    W32(L1B, CHC + 4, BIN); W32(L1B, CHC + 8, AIN);
    W32(L1A, CTL + 0x24, 4); W32(L1B, CTL + 0x24, 4); W32(L1A, CTL + 0x10, M_FWD); W32(L1A, CTL + 0x18, F_FENCE); W32(L1A, CTL + 0x1c, 1);
    W32(L1B, CTL + 0x10, M_REFLECT); W32(L1B, CTL + 0x18, F_FENCE | F_REGEN); W32(L1B, CTL + 0x1c, 1); W32(L1B, CTL + 0x20, 1);
    struct node na = {L1A, L1B}, nb = {L1B, L1A};
    pthread_t ta, tb; pthread_create(&ta, 0, run, &na); pthread_create(&tb, 0, run, &nb);
    /* host: feed 40 cars round-robin into slots 0..3, waiting for each return (nonce+1, state flipped) */
    u32 nonce = 1; int bad = 0;
    for (u32 k = 0; k < 40; ++k) {
      u32 i = k & 3, slot = AOUT + i * CAR;
      fillcar(L1A, slot, nonce, tcms, 3, (0 << 2) | i);   /* B's salt is (c<<2)|i with c=0 */
      u32 want = mk_hdr((nonce + 1) & 0xff, tcms, 4), got;
      u32 spin = 0;
      for (;;) { got = R32(L1A, AIN + i * CAR); if (got == want) break; if (++spin > 200000000u) { bad = 1; break; } }
      if (bad) { fprintf(stderr, "timeout k=%u tcms=%u\n", k, tcms); break; }
      /* verify returned payload regenerated for nonce+1 */
      u32 bytes = pkt_bytes(tcms), words = bytes / 4, fi = words - 1, ai = tcms >= 2 ? words - 5 : fi;
      for (u32 w = 1; w < words; ++w) { if (w == fi || w == ai) continue; if (R32(L1A, AIN + i*CAR + 4*w) != payload_word(want >> 8 & 0xff, w, i)) { bad = 1; fprintf(stderr, "payload bad k=%u w=%u\n", k, w); break; } }
      if (bad) break;
      nonce = (nonce + 1) & 0xff; if (!nonce) nonce = 1;
      nonce = (nonce + 1) & 0xff;  /* distinct from returned header so reuse is "changed" */
    }
    for (u32 s = 0; !bad && R32(L1A, CHC + 4*S_INSEEN) < 40 && s < 200000000u; ++s) {}
    W32(L1A, CTL + 4, 1); W32(L1B, CTL + 4, 1); pthread_join(ta, 0); pthread_join(tb, 0);
    u32 fw = R32(L1A, CHC + 4*S_FWD), rf = R32(L1B, CHC + 4*S_REFL), vb = R32(L1B, CHC + 4*S_VBAD), vc = R32(L1B, CHC + 4*S_VCHK), ins = R32(L1A, CHC + 4*S_INSEEN);
    printf("tcms=%2u bytes=%4u fwd=%u refl=%u arrivalsA=%u vchk=%u vbad=%u %s\n", tcms, pkt_bytes(tcms), fw, rf, ins, vc, vb, (bad || fw != 40 || rf != 40 || ins != 40 || vb) ? "FAIL" : "ok");
    if (bad || fw != 40 || rf != 40 || ins != 40 || vb) ++fails;
  }
  return fails;
}

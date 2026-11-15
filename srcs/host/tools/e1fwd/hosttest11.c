/* hosttest11.c -- native harness for e1fwd11.c: node A (FWD) and node B (REFLECT) as threads with fake L1 and fake TXQ2. */
#define HOSTTEST 1
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef unsigned int u32;
__thread char* g_l1; __thread u32 g_txq[0x40];
static __thread char* g_peer;
__thread u32 g_nri[0x20];
static char* g_t6;   /* fake T6 L1 (shared between threads) */
void ht_noc_cmd(void) {
  u32 src = g_nri[0], dst = g_nri[0x0c/4], node = g_nri[0x14/4], n = g_nri[0x20/4];
  if (g_nri[0x1c/4] != 2 || n == 0 || n > 16384 || node != ((3u << 6) | 5u) || (src % 16) != (dst % 16)) { fprintf(stderr, "BAD NOC cmd\n"); exit(2); }
  volatile u32* s = (volatile u32*)(g_l1 + src); volatile u32* d = (volatile u32*)(g_t6 + dst);
  for (u32 i = 0; i < n / 4; ++i) d[i] = s[i];
  g_nri[0x40/4] = 0;
}
void ht_txq_cmd(void) {
  u32 src = g_txq[0x14/4], dst = g_txq[0x1c/4], n = g_txq[0x18/4];
  if (g_txq[4/4] != 2 || n == 0 || n > 1456 || (n & 15)) { fprintf(stderr, "BAD TXQ n=%u\n", n); exit(2); }
  volatile u32* s = (volatile u32*)(g_l1 + src); volatile u32* d = (volatile u32*)(g_peer + dst);
  for (u32 i = 0; i < n / 4; ++i) d[i] = s[i];
}
#include "e1fwd11.c"
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
      { u32 fw = pkt_bytes(tcms) - 4; while (!bad && R32(L1A, AIN + i * CAR + fw) != want) if (++spin > 400000000u) { bad = 1; break; } }
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

  /* ---- stage 2: mode 7 (A FWD -> link -> B NOCFWD -> fake T6) and mode 8 loop ---- */
  g_t6 = calloc(1, 0x100000);
  for (int t = 0; t < 6; ++t) {
    u32 tcms = tcmsl[t];
    memset(L1A, 0, 0x100000); memset(L1B, 0, 0x100000); memset(g_t6, 0, 0x100000);
    W32(L1A, CHC + 0, AOUT); W32(L1A, CHC + 4, AIN); W32(L1A, CHC + 8, BIN);
    W32(L1B, CHC + 4, BIN);
    W32(L1A, CTL + 0x24, 4); W32(L1B, CTL + 0x24, 4);
    W32(L1B, CTL + 0x44, (3u << 6) | 5u); W32(L1B, CTL + 0x48, (3u << 6) | 5u); W32(L1B, CTL + 0x4c, 0x50000);
    W32(L1A, CTL + 0x10, M_FWD); W32(L1A, CTL + 0x18, F_FENCE); W32(L1A, CTL + 0x1c, 1);
    W32(L1B, CTL + 0x10, M_NOCFWD); W32(L1B, CTL + 0x18, F_FENCE); W32(L1B, CTL + 0x1c, 1);
    struct node na = {L1A, L1B}, nb = {L1B, L1A};
    pthread_t ta, tb; pthread_create(&ta, 0, run, &na); pthread_create(&tb, 0, run, &nb);
    int bad = 0; u32 nonce = 1;
    for (u32 k = 0; k < 40 && !bad; ++k) {
      u32 i = k & 3; fillcar(L1A, AOUT + i * CAR, nonce, tcms, 3, (0 << 2) | i);
      u32 want = mk_hdr(nonce, tcms, 3), spin = 0;
      while (R32(g_t6, 0x50000 + i * CAR) != want) if (++spin > 200000000u) { bad = 1; break; }
      { u32 fw = pkt_bytes(tcms) - 4; while (!bad && R32(g_t6, 0x50000 + i * CAR + fw) != want) if (++spin > 400000000u) { bad = 1; break; } }
      if (bad) { fprintf(stderr, "stage2 timeout k=%u\n", k); break; }
      u32 bytes = pkt_bytes(tcms), words = bytes / 4, fi = words - 1, ai = tcms >= 2 ? words - 5 : fi;
      for (u32 w = 1; w < words; ++w) { u32 e = (w == fi || w == ai) ? want : payload_word(nonce, w, i); if (R32(g_t6, 0x50000 + i * CAR + 4 * w) != e) { bad = 1; fprintf(stderr, "stage2 payload k=%u w=%u\n", k, w); break; } }
      nonce = (nonce + 5) & 0xff; if (!nonce) nonce = 1;
    }
    for (u32 s = 0; !bad && R32(L1B, CHC + 4*S_NOCW) < 40 && s < 200000000u; ++s) {}
    W32(L1A, CTL + 4, 1); W32(L1B, CTL + 4, 1); pthread_join(ta, 0); pthread_join(tb, 0);
    u32 nw = R32(L1B, CHC + 4*S_NOCW);
    printf("stage2 mode7 tcms=%2u bytes=%4u nocw=%u %s\n", tcms, pkt_bytes(tcms), nw, (bad || nw != 40) ? "FAIL" : "ok");
    if (bad || nw != 40) ++fails;
  }
  {
    memset(L1B, 0, 0x100000); memset(g_t6, 0, 0x100000);
    W32(L1B, CTL + 0x44, (3u << 6) | 5u); W32(L1B, CTL + 0x48, (3u << 6) | 5u); W32(L1B, CTL + 0x4c, 0x50000); W32(L1B, CTL + 0x50, 66); W32(L1B, CTL + 0x24, 4);
    W32(L1B, CTL + 0x10, M_NOCLOOP); W32(L1B, CTL + 0x18, F_FENCE);
    struct node nb = {L1B, L1A}; pthread_t tb; pthread_create(&tb, 0, run, &nb);
    for (u32 s = 0; R32(L1B, CHC + 4*S_NOCW) < 1000 && s < 400000000u; ++s) {}
    W32(L1B, CTL + 4, 1); pthread_join(tb, 0);
    u32 n = R32(L1B, CHC + 4*S_NOCW); int bad = 0;
    for (u32 j = 0; j < 4 && n >= 4; ++j) {
      u32 h = R32(g_t6, 0x50000 + j * CAR), words = 1040;
      if ((h & 0xff) != 0x2c || R32(g_t6, 0x50000 + j * CAR + 4 * (words - 1)) != h || R32(g_t6, 0x50000 + j * CAR + 4 * (words - 5)) != h) bad = 1;
      for (u32 w = 1; w < words && !bad; ++w) { if (w == words - 1 || w == words - 5) continue; if (R32(g_t6, 0x50000 + j * CAR + 4 * w) != payload_word(0, w, j)) bad = 1; }
    }
    printf("stage2 mode8 loop: nocw=%u %s\n", n, (bad || n < 1000) ? "FAIL" : "ok");
    if (bad || n < 1000) ++fails;
  }
  return fails;
}

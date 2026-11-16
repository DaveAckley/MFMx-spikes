/* hosttest14.c -- native harness for e1fwd14.c (the bidirectional car router).
 *
 * Two E1 nodes A and B run as threads with fake L1s and a fake TXQ2 link; each also has a fake T6 per channel reached through
 * a fake NoC (node id -> buffer table).  The "host" plays the local T6s: it writes cars into a node's OUT ring (as a NoC
 * write from the T6 would) and checks what arrives in the far T6's ring.
 *
 * Build/run:  gcc -O2 -Wall -pthread hosttest14.c -o /tmp/hosttest14 && /tmp/hosttest14      (no riscv toolchain needed)
 *
 * Channels: 0..5 full duplex (both E1s run L_OUT|L_IN_NOC); 6: A->B link only with B counting arrivals (L_IN_COUNT, T6 must
 * stay untouched); 7: A->B delivered to B's T6 (one way).  Also checks duplicate suppression (an unchanged complete car is not
 * re-forwarded) and the S_PARTIAL counter (counts once per slot occupancy, not per poll).
 */
#define HOSTTEST 1
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
typedef unsigned int u32;
__thread char* g_l1; __thread u32 g_txq[0x40];
static __thread char* g_peer;
__thread u32 g_nri[0x20];
static __thread char** g_t6tab;      /* this node's fake T6 buffers, indexed by NoC node x (1..8) */
static const u32 NOCY = 3;
void ht_noc_cmd(void) {
  u32 src = g_nri[0], dst = g_nri[0x0c / 4], node = g_nri[0x14 / 4], n = g_nri[0x20 / 4];
  u32 x = node & 63u, y = node >> 6;
  if (g_nri[0x1c / 4] != 2 || n == 0 || n > 16384 || y != NOCY || x < 1 || x > 8 || (src % 16) != (dst % 16)) { fprintf(stderr, "BAD NOC cmd node=%u n=%u\n", node, n); exit(2); }
  volatile u32* s = (volatile u32*)(g_l1 + src); volatile u32* d = (volatile u32*)(g_t6tab[x] + dst);
  for (u32 i = 0; i < n / 4; ++i) d[i] = s[i];
  g_nri[0x40 / 4] = 0;
}
void ht_txq_cmd(void) {
  u32 src = g_txq[0x14 / 4], dst = g_txq[0x1c / 4], n = g_txq[0x18 / 4];
  if (g_txq[4 / 4] != 2 || n == 0 || n > 1456 || (n & 15)) { fprintf(stderr, "BAD TXQ n=%u\n", n); exit(2); }
  volatile u32* s = (volatile u32*)(g_l1 + src); volatile u32* d = (volatile u32*)(g_peer + dst);
  for (u32 i = 0; i < n / 4; ++i) d[i] = s[i];
}
#include "e1fwd14.c"

#define MEM 0x100000u
#define W32(base, a, v) (*(volatile u32*)((base) + (a)) = (v))
#define R32(base, a) (*(volatile u32*)((base) + (a)))
static char *L1A, *L1B;
static char *T6A[9], *T6B[9];        /* fake T6 buffers per node x; index 0 unused */
struct node { char* l1; char* peer; char** tab; };
static void* run(void* p) { struct node* n = p; g_l1 = n->l1; g_peer = n->peer; g_t6tab = n->tab; e1_run(); return 0; }

static const u32 RINGSZ = RING_CARS_MAX * CAR;
static u32 OUTB(u32 c) { return 0x10000u + c * RINGSZ; }
static u32 INB(u32 c) { return 0x40000u + c * RINGSZ; }
static const u32 T6BASE = 0x50000u;
static const u32 K = 24;
static u32 tcmsl[] = {0, 1, 2, 3, 10, 66};

static void fillcar(char* base, u32 slot, u32 n, u32 tcms, u32 state, u32 salt) {
  u32 bytes = pkt_bytes(tcms), words = bytes / 4, fi = words - 1, ai = tcms >= 2 ? words - 5 : fi;
  for (u32 w = 1; w < words; ++w) if (w != fi && w != ai) W32(base, slot + 4 * w, payload_word(n, w, salt));
  u32 h = mk_hdr(n, tcms, state);
  if (tcms >= 2) W32(base, slot + 4 * ai, h);
  W32(base, slot + 4 * fi, h); W32(base, slot, h);
}
/* returns 0 ok */
static int checkcar(char* base, u32 slot, u32 n, u32 tcms, u32 state, u32 salt) {
  u32 bytes = pkt_bytes(tcms), words = bytes / 4, fi = words - 1, ai = tcms >= 2 ? words - 5 : fi, h = mk_hdr(n, tcms, state);
  if (R32(base, slot) != h || R32(base, slot + 4 * fi) != h || (tcms >= 2 && R32(base, slot + 4 * ai) != h)) return 1;
  for (u32 w = 1; w < words; ++w) { if (w == fi || w == ai) continue; if (R32(base, slot + 4 * w) != payload_word(n, w, salt)) return 2; }
  return 0;
}
static int waithdr(char* base, u32 slot, u32 tcms, u32 want) {      /* wait for header and footer */
  u32 fw = pkt_bytes(tcms) - 4, spin = 0;
  while (R32(base, slot) != want || R32(base, slot + 4 * 0 + fw) != want) if (++spin > 300000000u) return 0;
  return 1;
}
static u32 nonce_for(u32 k, u32 c, u32 dir) { return ((k * 5 + c * 11 + dir * 17) % 250) + 1; }
static u32 salt_for(u32 dir, u32 c, u32 i) { return (dir << 8) | (c << 2) | i; }

static void config(char* l1, int isA) {
  memset(l1, 0, MEM);
  for (u32 c = 0; c < NCH_MAX; ++c) {
    u32 cb = CHB(c);
    W32(l1, cb + 4 * C_OUT, OUTB(c)); W32(l1, cb + 4 * C_IN, INB(c)); W32(l1, cb + 4 * C_PEERIN, INB(c));
    W32(l1, cb + 4 * C_T6NODE, (NOCY << 6) | (1 + c)); W32(l1, cb + 4 * C_T6BASE, T6BASE);
    u32 legs = L_OUT | L_IN_NOC;
    if (c == 6) legs = isA ? L_OUT : L_IN_COUNT;
    if (c == 7) legs = isA ? L_OUT : L_IN_NOC;
    W32(l1, cb + 4 * C_LEGS, legs);
  }
  W32(l1, CTL + 0x24, 4); W32(l1, CTL + 0x10, M_ROUTER); W32(l1, CTL + 0x18, F_FENCE); W32(l1, CTL + 0x1c, 8);
  W32(l1, CTL + 0x44, (1u << 6) | (isA ? 3 : 4));
}

static u32 stat(char* l1, u32 c, u32 s) { return R32(l1, CHB(c) + 4 * s); }

int main(void) {
  L1A = calloc(1, MEM); L1B = calloc(1, MEM);
  for (u32 x = 1; x <= 8; ++x) { T6A[x] = calloc(1, MEM); T6B[x] = calloc(1, MEM); }
  int fails = 0;
  for (int t = 0; t < 6; ++t) {
    u32 tcms = tcmsl[t];
    config(L1A, 1); config(L1B, 0);
    for (u32 x = 1; x <= 8; ++x) { memset(T6A[x], 0, MEM); memset(T6B[x], 0, MEM); }
    struct node na = {L1A, L1B, T6A}, nb = {L1B, L1A, T6B};
    pthread_t ta, tb; pthread_create(&ta, 0, run, &na); pthread_create(&tb, 0, run, &nb);
    int bad = 0;
    for (u32 k = 0; k < K && !bad; ++k) {
      u32 i = k & 3;
      /* inject on every channel/direction first (tests concurrency across channels), then wait for delivery */
      for (u32 c = 0; c < 8; ++c) {
        fillcar(L1A, OUTB(c) + i * CAR, nonce_for(k, c, 0), tcms, 3, salt_for(0, c, i));
        if (c < 6) fillcar(L1B, OUTB(c) + i * CAR, nonce_for(k, c, 1), tcms, 3, salt_for(1, c, i));
      }
      for (u32 c = 0; c < 8 && !bad; ++c) {
        if (c != 6) {                        /* A -> B delivered into B's T6 of channel c */
          u32 n = nonce_for(k, c, 0);
          if (!waithdr(T6B[1 + c], T6BASE + i * CAR, tcms, mk_hdr(n, tcms, 3))) { fprintf(stderr, "timeout A->B c=%u k=%u\n", c, k); bad = 1; break; }
          int e = checkcar(T6B[1 + c], T6BASE + i * CAR, n, tcms, 3, salt_for(0, c, i));
          if (e) { fprintf(stderr, "A->B bad c=%u k=%u e=%d\n", c, k, e); bad = 1; break; }
        }
        if (c < 6) {                         /* B -> A */
          u32 n = nonce_for(k, c, 1);
          if (!waithdr(T6A[1 + c], T6BASE + i * CAR, tcms, mk_hdr(n, tcms, 3))) { fprintf(stderr, "timeout B->A c=%u k=%u\n", c, k); bad = 1; break; }
          int e = checkcar(T6A[1 + c], T6BASE + i * CAR, n, tcms, 3, salt_for(1, c, i));
          if (e) { fprintf(stderr, "B->A bad c=%u k=%u e=%d\n", c, k, e); bad = 1; break; }
        }
      }
      if (bad) break;
      if (k == K - 1 && tcms != 0) {         /* slot is not "complete" until the last word lands */
      }
    }
    /* channel 6: arrivals counted only; wait for the count, then confirm B's T6 buffer was never written */
    for (u32 s = 0; !bad && stat(L1B, 6, S_INSEEN) < K && s < 300000000u; ++s) {}
    u32 c6touched = 0;
    for (u32 w = 0; w < 0x4100 / 4; ++w) if (R32(T6B[7], T6BASE + 4 * w)) c6touched = 1;

    /* duplicate suppression: clear a delivered slot in B's T6 (ch0 slot 0); an unchanged OUT car must NOT be re-forwarded */
    u32 fwd0 = stat(L1A, 0, S_FWD), nocw0 = stat(L1B, 0, S_NOCW);
    W32(T6B[1], T6BASE, 0);
    { struct timespec ts = {0, 3000000}; nanosleep(&ts, 0); }
    int dup = (R32(T6B[1], T6BASE) != 0) || stat(L1A, 0, S_FWD) != fwd0 || stat(L1B, 0, S_NOCW) != nocw0;

    /* partial counter: header-only write into the head slot (old footer) counts ONCE, then completing delivers.
       K is a multiple of 4, so the head slot of every ring is 0. */
    u32 p0 = stat(L1A, 0, S_PARTIAL);
    u32 pn = nonce_for(K + 7, 0, 0), ph = mk_hdr(pn, tcms, 3);
    int pbad = 0;
    if (tcms >= 1) {
      W32(L1A, OUTB(0), ph);                 /* header only */
      { struct timespec ts = {0, 3000000}; nanosleep(&ts, 0); }
      u32 p1 = stat(L1A, 0, S_PARTIAL);
      if (p1 != p0 + 1) { fprintf(stderr, "partial count %u -> %u (want +1)\n", p0, p1); pbad = 1; }
      fillcar(L1A, OUTB(0), pn, tcms, 3, salt_for(0, 0, 0));
      if (!waithdr(T6B[1], T6BASE, tcms, ph) || checkcar(T6B[1], T6BASE, pn, tcms, 3, salt_for(0, 0, 0))) { fprintf(stderr, "partial: completed car not delivered intact\n"); pbad = 1; }
    }

    W32(L1A, CTL + 4, 1); W32(L1B, CTL + 4, 1); pthread_join(ta, 0); pthread_join(tb, 0);
    int cbad = bad || c6touched || dup || pbad;
    for (u32 c = 0; c < 8; ++c) {
      u32 extra = (c == 0 && tcms >= 1) ? 1u : 0u;                 /* the partial test adds one more car on A ch0 */
      u32 wantA_fwd = K + extra, wantB_fwd = (c < 6) ? K : 0;
      u32 wantB_nocw = (c == 6) ? 0 : K + extra, wantA_nocw = (c < 6) ? K : 0;
      if (stat(L1A, c, S_FWD) != wantA_fwd || stat(L1B, c, S_FWD) != wantB_fwd || stat(L1B, c, S_NOCW) != wantB_nocw ||
          stat(L1A, c, S_NOCW) != wantA_nocw || stat(L1A, c, S_TXFAIL) || stat(L1B, c, S_TXFAIL) || stat(L1A, c, S_NOCFAIL) || stat(L1B, c, S_NOCFAIL)) {
        fprintf(stderr, "counters c=%u A: fwd=%u nocw=%u  B: fwd=%u nocw=%u txfail=%u/%u nocfail=%u/%u\n", c, stat(L1A, c, S_FWD), stat(L1A, c, S_NOCW),
                stat(L1B, c, S_FWD), stat(L1B, c, S_NOCW), stat(L1A, c, S_TXFAIL), stat(L1B, c, S_TXFAIL), stat(L1A, c, S_NOCFAIL), stat(L1B, c, S_NOCFAIL));
        cbad = 1;
      }
    }
    printf("tcms=%2u bytes=%4u sweepsA=%u sweepsB=%u partialA(ch0)=%u dup=%d c6touched=%u %s\n", tcms, pkt_bytes(tcms), R32(L1A, CTL + 0x30), R32(L1B, CTL + 0x30),
           stat(L1A, 0, S_PARTIAL), dup, c6touched, cbad ? "FAIL" : "ok");
    if (cbad) ++fails;
  }
  printf("%s\n", fails ? "SOME TESTS FAILED" : "all ok");
  return fails;
}

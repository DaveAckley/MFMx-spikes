/* e1order14.c (successor of e1order13.c / e1order.c, which stay as the sources of ethlinktest13/12's embedded images) -- diagnostic RISC-V E1 image for Blackhole Ethernet tiles (eth-seam).
 *
 * PURPOSE: observe, at core speed (L1 loads back-to-back; clock NOT assumed -- reported to be ~800 MHz on the
 * test machine, 1.35 GHz is only the nominal max, so ~9 ns per L1 load), the ORDER in which the Ethernet RX queue
 * commits an incoming TT-link L1-write packet to this tile's L1.  MFMx packets carry a nonce in a header word,
 * an "ankle" word 5 words before the footer, and a footer word; the question is whether, when the footer
 * is visible, the ankle/header/payload are also already visible.  A host over PCIe cannot see this (a PCIe read
 * takes ~0.85 us; a packet lands in tens of ns).
 *
 * OBSERVER ONLY: this image reads one L1 slot, and writes only a control/results block in L1 (CTL=0x8000)
 * and its own stack in E1 local RAM.  It touches no NIU, no queue, no register.
 *
 * PROTOCOL (host side in ethlinktest12.cpp):
 *   CTL+0x00 go    CTL+0x04 stop   CTL+0x08 ready(0xE1E1E1E1)  CTL+0x0C done
 *   CTL+0x10 slot L1 address  CTL+0x14 size bytes  CTL+0x18 K (power of two, nonce cycle length)
 *   CTL+0x1C verify-every-N full-payload verifications
 *   CTL+0x20 mode: 2 = L0-CACHE SELF-TEST without fence, 3 = same with fence: poll slot word 0 while the host writes an
 *            incrementing counter there; reports changes seen / values missed (R_ALLSAME / R_INVALID, R_SAMPLES = polls)
 *            0 = INDEPENDENT loads footer,ankle,mid,header (as test12; the load issue/service order is NOT
 *            guaranteed by the core: loads to different L1 banks may be serviced out of order);
 *            1 = DEPENDENT chain footer->ankle->header->mid (each address depends on the previous load's value via an
 *            asm 'and rd,rs,zero', so each load is issued after the previous one returned), and when header==ankle==footer
 *            (complete by rule) immediately issue dependent PROBE loads of 4 payload words (mid, 4 back from ankle,
 *            4 back from footer, word 4): any probe OLDER than the footer's version is a rule violation.
 *   RES = CTL+0x40: see R_* below; joint histogram at RES+16*4 .. (27 words)
 *
 * PACKET LAYOUT (matches TCMarker.h): word0 = header, word[words-1] = footer, word[words-5] = ankle (size >= 64),
 * marker word = (tcms<<16)|(nonce<<8)|0x2C, tcms = 1 for 32 B, else size/64+1.  All other words (including
 * the mid word) = (v*0x01010101) ^ (idx*0x9E3779B1), v = nonce.
 *
 * LANE MODEL (per user): NoC->L1 delivers two independent 16 B-per-write lanes (byte addr 0..15 and 16..31 mod 32),
 * each in-order, no ordering BETWEEN lanes.  So 'older than footer' for a word in the other lane is EXPECTED.  The
 * claim to test is the MFMx rule: header==ankle==footer  =>  all other words of that version already landed.
 * R_TORN now counts verifications with a payload word OLDER than the footer's version (a real violation of the
 * rule); R_NEWERW counts verifications that saw a NEWER word (overwrite race); R_GARB unclassifiable words.
 *
 * Reads happen in the order footer, ankle, mid, header.  If the commit order is header..footer in address
 * order, then once the footer of version v is seen, ankle/mid/header can only be v ("same") or later ("newer").
 * An "older" class means the footer overtook that word.  SENSITIVITY: with read spacing d (one dependent L1 load
 * plus loop glue, ~10 ns at 800 MHz) mode 0 can only see a word that lags the footer by more than d (ankle),
 * 2d (mid), 3d (header).  Mode 1 reads footer last, so 'older' there is expected whenever a packet lands
 * mid-sample; its rate times the packet interarrival time estimates the span of one four-word sample.
 *
 * Build (see build.sh): clang --target=riscv32 -march=rv32im_zicsr -mabi=ilp32 -O2 -ffreestanding -nostdlib
 */
typedef unsigned int u32;
/* The baby RISC-V L0 data cache (per MemoryOrdering.md, Tensix tile; assumed same on the ETH E1 core): 4 lines x 16 B,
 * NOT coherent with L1 writes by other clients, flushed only by fence/atomics or ~0.8%-per-hit randomly.  Polling L1
 * written by someone else MUST fence first or loads can return data that is microseconds old.  ethlinktest12/13
 * did not, so their ordering results are suspect.  FENCE() also drains the store queue and waits for in-flight loads. */
#ifdef HOSTTEST
#define FENCE() __asm__ volatile("" ::: "memory")
#else
#define FENCE() __asm__ volatile("fence" ::: "memory")
#endif
#ifdef HOSTTEST
extern char* g_l1;
#define P(a) ((volatile u32*)(g_l1 + (a)))
#else
typedef unsigned long uintptr_t_;
#define P(a) ((volatile u32*)(uintptr_t_)(a))
#endif

#define CTL 0x8000u
#define RES (CTL + 0x40u)
enum { R_SAMPLES = 0, R_INVALID = 1, R_ALLSAME = 2, R_VERIFIED = 3, R_RECHECK_SKIP = 4, R_TORN = 5, R_TORN_WORD = 6,
       R_TORN_GOT = 7, R_TORN_EXP = 8, R_TORN_NONCE = 9, R_NEWERW = 10, R_GARB = 11, R_IMM_CHECKS = 12, R_IMM_OLDER = 13, R_IMM_NEWER = 14, R_IMM_FIRSTWORD = 15,
       R_JOINT = 16, R_PROBE_OLDER = 43 /* 4 words */ };

static inline u32 magic_ok(u32 w) { return (w & 0xffu) == 0x2cu; }
static inline u32 nonce_of(u32 w) { return (w >> 8) & 0xffu; }
/* 0 = same, 1 = newer than f, 2 = older than f; K is a power of two, mask = K-1 */
static inline u32 cls(u32 x, u32 f, u32 mask) {
  u32 d = (x - f) & mask;
  return d == 0 ? 0u : (d <= (mask >> 1) ? 1u : 2u);
}

#ifdef HOSTTEST
static inline u32 dep0(u32 v) { u32 z = 0; __asm__ volatile("" : "+r"(z) : "r"(v)); return z; }
#else
static inline u32 dep0(u32 v) { u32 z; __asm__ volatile("and %0, %1, zero" : "=r"(z) : "r"(v)); return z; }  /* z==0, but depends on v */
#endif
static inline u32 pick(u32 idx, u32 ai, u32 fi) { while (idx > 1u && (idx == ai || idx == fi)) --idx; return idx ? idx : 1u; }

static inline __attribute__((always_inline)) void monitor_body(u32 dep) {
  u32 slot = *P(CTL + 0x10), size = *P(CTL + 0x14), K = *P(CTL + 0x18), every = *P(CTL + 0x1c);
  u32 words = size / 4u, fi = words - 1u, ai = size >= 64u ? words - 5u : fi, mi = words / 2u, mask = K - 1u;
  u32 pr[4];
  pr[0] = pick(mi, ai, fi); pr[1] = pick(ai > 4u ? ai - 4u : 1u, ai, fi); pr[2] = pick(fi > 4u ? fi - 4u : 1u, ai, fi); pr[3] = pick(4u, ai, fi);
  volatile u32* sl = P(slot);
  volatile u32* joint = P(RES + 4u * R_JOINT);
  u32 n = 0, inv = 0, same = 0, verified = 0, skip = 0, torn = 0, newerw = 0, garb = 0, countdown = every;
  u32 imm_checks = 0, imm_older = 0, imm_newer = 0, imm_first = 0, probe_old[4] = {0, 0, 0, 0};
  for (;;) {
    FENCE();
    if (*P(CTL + 4)) break;
    FENCE();
    u32 f, a, m, h;
    if (dep) { f = sl[fi]; FENCE(); a = sl[ai + dep0(f)]; FENCE(); h = sl[dep0(a)]; FENCE(); m = sl[mi + dep0(h)]; }
    else     { f = sl[fi]; a = sl[ai]; m = sl[mi]; h = sl[0]; }
    ++n;
    if ((n & 0xffffu) == 0) *P(RES + 4u * R_SAMPLES) = n;
    if (!(magic_ok(f) && magic_ok(a) && magic_ok(h))) { ++inv; continue; }
    u32 nf = nonce_of(f);
    u32 ca = cls(nonce_of(a), nf, mask), ch = cls(nonce_of(h), nf, mask);
    u32 cm = cls((m ^ (mi * 0x9E3779B1u)) & 0xffu, nf, mask);
    u32 j = ca * 9u + cm * 3u + ch;
    joint[j] = joint[j] + 1u;
    if (j == 0) ++same;
    if (ca == 0u && ch == 0u) {
      if (dep) {
        /* complete by rule as of the header load; probe immediately, each load after the previous returned */
        u32 z = dep0(m), any_old = 0, any_new = 0;
        for (u32 q = 0; q < 4u; ++q) {
          FENCE();
          u32 w = pr[q], got = sl[w + z], x = got ^ (w * 0x9E3779B1u), b0 = x & 0xffu;
          z = dep0(got);
          if (x != b0 * 0x01010101u) continue;
          u32 c = cls(b0, nf, mask);
          if (c == 2u) { ++probe_old[q]; if (!any_old && !imm_first) imm_first = w; any_old = 1; }
          else if (c == 1u) any_new = 1;
        }
        ++imm_checks; imm_older += any_old; imm_newer += any_new;
      }
      if (--countdown == 0) {
        countdown = every;
        u32 nold = 0, nnew = 0, ngar = 0, bw = 0, bgot = 0, bexp = 0;
        FENCE();
        for (u32 w = 1; w < words; ++w) {
          if (w == ai || w == fi) continue;
          u32 got = sl[w], base = w * 0x9E3779B1u, x = got ^ base, b0 = x & 0xffu;
          if (x != b0 * 0x01010101u) { ++ngar; continue; }
          u32 c = cls(b0, nf, mask);
          if (c == 2u) { if (nold == 0) { bw = w; bgot = got; bexp = (nf * 0x01010101u) ^ base; } ++nold; }
          else if (c == 1u) ++nnew;
        }
        FENCE();
        u32 h2 = sl[0];
        ++verified;
        if (!magic_ok(h2) || nonce_of(h2) != nf) ++skip;
        if (nnew) ++newerw;
        if (ngar) ++garb;
        if (nold) {
          if (torn == 0) {
            *P(RES + 4u * R_TORN_WORD) = bw; *P(RES + 4u * R_TORN_GOT) = bgot;
            *P(RES + 4u * R_TORN_EXP) = bexp; *P(RES + 4u * R_TORN_NONCE) = nf;
          }
          ++torn;
        }
      }
    }
  }
  *P(RES + 4u * R_SAMPLES) = n; *P(RES + 4u * R_INVALID) = inv; *P(RES + 4u * R_ALLSAME) = same;
  *P(RES + 4u * R_VERIFIED) = verified; *P(RES + 4u * R_RECHECK_SKIP) = skip; *P(RES + 4u * R_TORN) = torn;
  *P(RES + 4u * R_NEWERW) = newerw; *P(RES + 4u * R_GARB) = garb;
  *P(RES + 4u * R_IMM_CHECKS) = imm_checks; *P(RES + 4u * R_IMM_OLDER) = imm_older; *P(RES + 4u * R_IMM_NEWER) = imm_newer;
  *P(RES + 4u * R_IMM_FIRSTWORD) = imm_first;
  for (u32 q = 0; q < 4u; ++q) *P(RES + 4u * (R_PROBE_OLDER + q)) = probe_old[q];
}

static inline __attribute__((always_inline)) void l0test(u32 fence) {
  u32 slot = *P(CTL + 0x10);
  volatile u32* sl = P(slot);
  u32 last = sl[0], n = 0, changes = 0, missed = 0;
  for (;;) {
    if (fence) FENCE();
    if (*P(CTL + 4)) break;
    u32 v = sl[0];
    ++n;
    if (v != last) { missed += v - last - 1u; ++changes; last = v; }
  }
  *P(RES + 4u * R_SAMPLES) = n; *P(RES + 4u * R_ALLSAME) = changes; *P(RES + 4u * R_INVALID) = missed;
}

#ifdef HOSTTEST
void monitor(void)
#else
static void monitor(void)
#endif
{
  u32 mode = *P(CTL + 0x20);
  if (mode >= 2u) { if (mode == 3u) l0test(1u); else l0test(0u); }
  else if (mode) monitor_body(1u); else monitor_body(0u);
}

#ifndef HOSTTEST
void main_e1(void) {
  *P(CTL + 0x08) = 0xE1E1E1E1u;
  for (;;) {
    while (!*P(CTL + 0x00)) {}
    *P(CTL + 0x0c) = 0;
    monitor();
    *P(CTL + 0x0c) = 1;
    while (*P(CTL + 0x00)) {}
  }
}
__asm__(".section .text.start,\"ax\"\n.globl _start\n_start:\n  li sp, 0xFFB02000\n  call main_e1\n1: j 1b\n");
#endif

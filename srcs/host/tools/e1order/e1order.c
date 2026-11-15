/* e1order.c -- diagnostic RISC-V E1 image for Blackhole Ethernet tiles (eth-seam).
 *
 * PURPOSE: observe, at core speed (~1.35 GHz, L1 loads back-to-back), the ORDER in which the Ethernet RX queue
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
 *   RES = CTL+0x40: see R_* below; joint histogram at RES+16*4 .. (27 words)
 *
 * PACKET LAYOUT (matches TCMarker.h): word0 = header, word[words-1] = footer, word[words-5] = ankle (size >= 64),
 * marker word = (tcms<<16)|(nonce<<8)|0x2C, tcms = 1 for 32 B, else size/64+1.  All other words (including
 * the mid word) = (v*0x01010101) ^ (idx*0x9E3779B1), v = nonce.
 *
 * Reads happen in the order footer, ankle, mid, header.  If the commit order is header..footer in address
 * order, then once the footer of version v is seen, ankle/mid/header can only be v ("same") or later ("newer").
 * An "older" class means the footer overtook that word.
 *
 * Build (see build.sh): clang --target=riscv32 -march=rv32im_zicsr -mabi=ilp32 -O2 -ffreestanding -nostdlib
 */
typedef unsigned int u32;
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
       R_TORN_GOT = 7, R_TORN_EXP = 8, R_TORN_NONCE = 9, R_JOINT = 16 };

static inline u32 magic_ok(u32 w) { return (w & 0xffu) == 0x2cu; }
static inline u32 nonce_of(u32 w) { return (w >> 8) & 0xffu; }
/* 0 = same, 1 = newer than f, 2 = older than f; K is a power of two, mask = K-1 */
static inline u32 cls(u32 x, u32 f, u32 mask) {
  u32 d = (x - f) & mask;
  return d == 0 ? 0u : (d <= (mask >> 1) ? 1u : 2u);
}

#ifdef HOSTTEST
void monitor(void)
#else
static void monitor(void)
#endif
{
  u32 slot = *P(CTL + 0x10), size = *P(CTL + 0x14), K = *P(CTL + 0x18), every = *P(CTL + 0x1c);
  u32 words = size / 4u, fi = words - 1u, ai = size >= 64u ? words - 5u : fi, mi = words / 2u, mask = K - 1u;
  volatile u32* sl = P(slot);
  volatile u32* joint = P(RES + 4u * R_JOINT);
  u32 n = 0, inv = 0, same = 0, verified = 0, skip = 0, torn = 0, countdown = every;
  for (;;) {
    if (*P(CTL + 4)) break;
    u32 f = sl[fi];
    u32 a = sl[ai];
    u32 m = sl[mi];
    u32 h = sl[0];
    ++n;
    if ((n & 0xffffu) == 0) *P(RES + 4u * R_SAMPLES) = n;
    if (!(magic_ok(f) && magic_ok(a) && magic_ok(h))) { ++inv; continue; }
    u32 nf = nonce_of(f);
    u32 ca = cls(nonce_of(a), nf, mask), ch = cls(nonce_of(h), nf, mask);
    u32 cm = cls((m ^ (mi * 0x9E3779B1u)) & 0xffu, nf, mask);
    u32 j = ca * 9u + cm * 3u + ch;
    joint[j] = joint[j] + 1u;
    if (j == 0) {
      ++same;
      if (--countdown == 0) {
        countdown = every;
        u32 bad = 0, bw = 0, bgot = 0, bexp = 0;
        for (u32 w = 1; w < words; ++w) {
          if (w == ai || w == fi) continue;
          u32 got = sl[w], exp = (nf * 0x01010101u) ^ (w * 0x9E3779B1u);
          if (got != exp) { bad = 1; bw = w; bgot = got; bexp = exp; break; }
        }
        u32 h2 = sl[0];
        if (!magic_ok(h2) || nonce_of(h2) != nf) ++skip;
        else {
          ++verified;
          if (bad) {
            if (torn == 0) {
              *P(RES + 4u * R_TORN_WORD) = bw; *P(RES + 4u * R_TORN_GOT) = bgot;
              *P(RES + 4u * R_TORN_EXP) = bexp; *P(RES + 4u * R_TORN_NONCE) = nf;
            }
            ++torn;
          }
        }
      }
    }
  }
  *P(RES + 4u * R_SAMPLES) = n; *P(RES + 4u * R_INVALID) = inv; *P(RES + 4u * R_ALLSAME) = same;
  *P(RES + 4u * R_VERIFIED) = verified; *P(RES + 4u * R_RECHECK_SKIP) = skip; *P(RES + 4u * R_TORN) = torn;
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

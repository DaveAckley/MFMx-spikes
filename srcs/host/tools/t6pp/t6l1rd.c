/* t6l1rd.c -- bare-metal Tensix image that tests how the NIU places the data
 * of a NoC READ from ANOTHER TILE'S L1 into this tile's L1, as a function of
 * the source address and the return-buffer address.  New file
 * (srcs/host/tools/t6pp/), built by t6pp/build.sh t6l1rd, run by t6l1rdrun.
 * Image id 0x76500020.  Read-only toward the peer tile.
 *
 * Why: reads from the ARC tile land at (return address & ~63) + (source
 * address & 63) (t6arc3/t6arc4, ERISC_NOTES 9.19), but NRI3::blockingL1ReadNoC0
 * (NRIUtils.cpp) matches its return buffer to the source only modulo 16, and
 * MFMx reads other T6s' L1 with it (image block headers at 0xb0, 48 mod 64)
 * and that works.  So the rule must differ for an L1 source.  This measures it.
 *
 * The host fills the peer's L1 at PEERB (0x20000) with val(a) = a * 0x9E3779B1
 * ^ 0x5A5A5A5A for every word address a.  For each of 4 buffer alignments k
 * (buffer base RB + 16 k), 16 source offsets S (source PEERB + 4 S, so source
 * mod 64 = 4 S) and 3 word counts (1, 4, 12), method A reads with buf = base +
 * (src & 15) (exactly what blockingL1ReadNoC0 does: base alignment k*16, then
 * matched to the source modulo 16), and method B reads with buf = RB + (src &
 * 63) (the ARC rule).  Each read is checked against val(); a verdict word is
 * 1 if the N words at buf are right, else 0, with bits 8.. = 1 + the word index
 * (in the 64-word buffer region) where val(src) was found, 0 if not found.
 *
 * Control block at CTLB (T6 L1 0x8000), window 0x900:
 *   +00 go  +04 stop  +08 ready (0x7E57A11E)  +0C done  +14 NoC (0/1)
 *   +18 own node (y<<6|x)  +1C peer node  +20 NIU command buffer (0 = 3)
 *   +3C image id  +40 pass counter  +44 csr 0x7c0 readback (|0x80000000)
 * Results at +100: method A verdicts [k 0..3][S 0..15][n 0..2] (192 words,
 * index (k*16+S)*3+n); +400: method B [S][n] (48 words); +4C0 reads done,
 * +4C4 reads failed.  Buffer region at +800 (RB), 256 bytes.
 */
typedef unsigned int u32;
#define T6L1RD_ID 0x76500020u
#ifndef CTLB
#define CTLB 0x8000u
#endif
#define CT(o) (CTLB + (o))
#define RB (CTLB + 0x800u)
#define PEERB 0x20000u
#define SPIN_MAX 4000000u
#define TXID 0xeu

#ifdef HOSTTEST
extern char* g_l1;
extern volatile u32 g_niu[];
extern void ht_noc_go(u32 nb);
#define L1(a) ((volatile u32*)(g_l1 + (a)))
#define NIUX(base, off) (&g_niu[(((base) - 0xFFB20000u) + (off)) / 4u])
static inline void csr_set_l0off(void) {}
static inline u32 csr_read_l0(void) { return 8u; }
#else
typedef unsigned long uintptr_t_;
#define L1(a) ((volatile u32*)(uintptr_t_)(a))
#define NIUX(base, off) ((volatile u32*)(uintptr_t_)((base) + (off)))
static inline void csr_set_l0off(void) {
  u32 v = 8u;
  __asm__ volatile("csrrs x0, 0x7c0, %0" ::"r"(v));
}
static inline u32 csr_read_l0(void) {
  u32 v;
  __asm__ volatile("csrr %0, 0x7c0" : "=r"(v));
  return v;
}
#endif

struct ctx {
  u32 niu, nb, own, peer, done, failed;
};

static inline u32 val(u32 a) { return (a * 0x9E3779B1u) ^ 0x5A5A5A5Au; }

/* read n words at src of the peer's L1 into OUR L1 at buf */
static int rd(struct ctx* c, u32 src, u32 n, u32 buf) {
  u32 spin = 0;
  while (*NIUX(c->niu, 0x200u + (16u + TXID) * 4u) & 0xffu)
    if (++spin > SPIN_MAX) { ++c->failed; return 0; }
  while (*NIUX(c->nb, 0x40) & 1u)
    if (++spin > SPIN_MAX) { ++c->failed; return 0; }
  *NIUX(c->nb, 0x00) = src;
  *NIUX(c->nb, 0x04) = 0;
  *NIUX(c->nb, 0x08) = c->peer;
  *NIUX(c->nb, 0x0c) = buf;
  *NIUX(c->nb, 0x10) = 0;
  *NIUX(c->nb, 0x14) = c->own;
  *NIUX(c->nb, 0x18) = TXID << 10;
  *NIUX(c->nb, 0x1c) = 0u;
  *NIUX(c->nb, 0x20) = n << 2;
  *NIUX(c->nb, 0x24) = 0;
  *NIUX(c->nb, 0x40) = 1u;
#ifdef HOSTTEST
  ht_noc_go(c->nb);
#endif
  { volatile u32 rb = *NIUX(c->nb, 0x40); (void)rb; }
  while (*NIUX(c->niu, 0x200u + (16u + TXID) * 4u) & 0xffu)
    if (++spin > SPIN_MAX) { ++c->failed; return 0; }
  ++c->done;
  return 1;
}

/* one trial: fill the region with a sentinel, read, judge */
static u32 trial(struct ctx* c, u32 src, u32 n, u32 buf) {
  u32 i, ok = 1u, found = 0u;
  for (i = 0; i < 64u; ++i) *L1(RB + 4u * i) = 0xDEADBEEFu;
  if (!rd(c, src, n, buf)) return 0xFFu;
  for (i = 0; i < n; ++i)
    if (*L1(buf + 4u * i) != val(src + 4u * i)) ok = 0u;
  if (!ok) {
    for (i = 0; i < 64u; ++i)
      if (*L1(RB + 4u * i) == val(src)) { found = i + 1u; break; }
  }
  return ok | (found << 8);
}

static void pass(struct ctx* c) {
  static const u32 NS[3] = { 1u, 4u, 12u };
  u32 k, s, n;
  c->done = 0;
  c->failed = 0;
  for (k = 0; k < 4u; ++k)
    for (s = 0; s < 16u; ++s)
      for (n = 0; n < 3u; ++n) {
        u32 src = PEERB + 4u * s;
        u32 base = RB + 16u * k;
        u32 buf = base + (src & 15u);
        *L1(CT(0x100u + 4u * ((k * 16u + s) * 3u + n))) =
            trial(c, src, NS[n], buf);
      }
  for (s = 0; s < 16u; ++s)
    for (n = 0; n < 3u; ++n) {
      u32 src = PEERB + 4u * s;
      u32 buf = RB + (src & 63u);
      *L1(CT(0x400u + 4u * (s * 3u + n))) = trial(c, src, NS[n], buf);
    }
  *L1(CT(0x4C0)) = c->done;
  *L1(CT(0x4C4)) = c->failed;
}

#ifdef HOSTTEST
void ht_pass(u32 noc, u32 own, u32 peer) {
  struct ctx c;
  c.niu = noc ? 0xFFB30000u : 0xFFB20000u;
  c.nb = c.niu + 3u * 0x800u;
  c.own = own;
  c.peer = peer;
  pass(&c);
}
#else
static void run(void) {
  struct ctx c;
  u32 noc = *L1(CT(0x14)) & 1u;
  u32 buf = *L1(CT(0x20));
  if (buf == 0u) buf = 3u;
  if (buf > 3u) buf = 3u;
  c.niu = noc ? 0xFFB30000u : 0xFFB20000u;
  c.nb = c.niu + buf * 0x800u;
  c.own = *L1(CT(0x18));
  c.peer = *L1(CT(0x1C));
  *L1(CT(0x44)) = 0u;
  csr_set_l0off();
  *L1(CT(0x44)) = csr_read_l0() | 0x80000000u;
  for (;;) {
    while (!*L1(CT(0x00))) {
      if (*L1(CT(0x04))) return;
    }
    *L1(CT(0x0C)) = 0;
    pass(&c);
    *L1(CT(0x40)) = *L1(CT(0x40)) + 1u;
    *L1(CT(0x0C)) = 1;
    while (*L1(CT(0x00))) {
      if (*L1(CT(0x04))) return;
    }
  }
}

void main_t6(void) {
  *L1(CT(0x3C)) = T6L1RD_ID;
  *L1(CT(0x08)) = 0x7E57A11Eu;
  run();
  *L1(CT(0x0C)) = 2;
  for (;;) {}
}

__asm__(".section .text.start,\"ax\"\n.globl _start\n_start:\n  j start_b\n"
        "  j hang\n  j hang\n  j hang\n  j start_nc\n"
        "hang: j hang\n"
        "start_b:\n  li sp, 0xFFB02000\n  call main_t6\n1: j 1b\n"
        "start_nc:\n  li sp, 0xFFB02000\n  call main_t6\n2: j 2b\n.previous\n");
#endif

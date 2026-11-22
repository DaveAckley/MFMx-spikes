/* t6arc3.c -- t6arc2.c plus a SWEEP probe (image id 0x76500012); t6arc2.c is unchanged.
 * Why: t6arc2 showed that a 12-word block read at 0x80030400 delivers correctly (scratch 0,
 * 2, 10, 11 right at the right buffer words) and a single-word read of scratch 2 is right,
 * but single reads of scratch 11 (0x8003042c) and a 4-word read at 0x80030430 return 0.
 * Probe (control +24 = 1) now does, into a result area at +0x600: S1 single-word reads of
 * scratch 0..15 (+0x00), S2 the same with a delay before the copy (+0x40), B0/B1/B2 12-word
 * reads at 0x80030400/410/430 (+0x80/+0xB0/+0xE0), 2 words at 0x1001a094 (+0x110) and 12 at
 * 0x1001a1a0 (+0x120) (ARC SRAM: the table header and data, addresses seen by arcpeek10.out),
 * 4 words at 0x80030430 (+0x150), 2 words at 0x8003042c (+0x160).
 *
 * ---- t6arc2.c header follows ----
 * t6arc2.c -- t6arc.c plus a DEBUG LOG and a PROBE mode (image id 0x76500011); t6arc.c is
 * unchanged.  Why: t6arc read the ARC boot status (scratch 2 = 5) correctly but scratch
 * 11, 12, 13 as 0 (run 2026-11-22).  The log keeps, for each of the first 8 NoC reads, the
 * address, the buffer address and the 16 words of the read buffer afterwards (at +0x300,
 * 18 words per entry); control word +24 = 1 turns on the PROBE reads that run before the
 * normal walk: 12 words at 0x80030400, 4 words at 0x80030430, scratch 11 three times,
 * scratch 2 then scratch 12.
 *
 * ---- t6arc.c header follows ----
 * t6arc.c -- bare-metal Tensix image that READS the Blackhole ARC tile's
 * telemetry over the NoC and leaves the numbers in L1 for the host.  New file
 * (srcs/host/tools/t6pp/), built by t6pp/build.sh t6arc (sfpi gcc, linked at
 * L1 address 0, BRISC reset address), run by host tool t6arcrun.
 *
 * It only READS (NoC read commands to the ARC tile); it never writes to the
 * ARC.  It is the T6-side twin of arcpeek: same registers, same table walk.
 *   ARC scratch registers 0x80030400 + 4N: N=2 boot status, 11 queue info,
 *   12 telemetry data base, 13 telemetry table pointer.
 *   Table at ptr: word0 version, word1 entry count, entries from ptr+8:
 *   low 16 bits tag id, high 16 bits offset (words) from the data base.
 * NoC read command sequence = NRI3::blockingL1ReadNoC0 (srcs/cross/src/
 * NRIUtils.cpp): command buffer 3 of NIU0 (or NIU1), read command (CTRL 0),
 * transaction id 0xe, completion = the NIU outstanding counter for that id
 * returns to 0.  The return address in our L1 must match the source address
 * modulo 16 (the Wormhole rule, assumed for Blackhole as NRIUtils does).
 *
 * Control block at CTLB (T6 L1 0x8000), window 0x600, written by the host
 * BEFORE the core is released, except go:
 *   +00 go  +04 stop  +08 ready (0x7E57A11E written by the image)  +0C done
 *   +14 NoC (0 = NIU0, 1 = NIU1)  +18 own node (y<<6|x, NoC0 raw)
 *   +1C ARC node (y<<6|x; (8,0) = 8: the ARC column maps to itself on both)
 *   +20 NIU command buffer (0 = default 3, 0xFF = buffer 0, 1..3 as is)
 *   +3C image id  +40 pass counter  +44 csr 0x7c0 readback (|0x80000000)
 * One pass runs each time the host raises go (go is lowered to run another).
 * Results at +100, rewritten every pass:
 *   +100 status (0 ok, 1 bad pointer, 2 read timeout, 3 bad entry count,
 *        4 tag missing)    +104 reads done   +108 reads failed
 *   +10C boot status       +110 queue info ptr   +114 data base
 *   +118 table ptr         +11C table version    +120 entry count
 *   +124 AICLK raw         +128 AXICLK raw       +12C ARCCLK raw
 *   +130 heartbeat         +134 AICLK_LIMIT_MAX  +138 ASIC_TEMP raw
 *   +13C POWER raw         +140 VCORE raw        +144 AICLK offset found
 *   +148 L2CPUCLK0 raw (tag 17)
 * Scratch read buffer at +200 (16-aligned), 32 words.
 */
typedef unsigned int u32;
#define T6ARC_ID 0x76500012u
#ifndef CTLB
#define CTLB 0x8000u
#endif
#define CT(o) (CTLB + (o))
#define RB (CTLB + 0x200u)

#ifdef HOSTTEST
/* host build for t6pp/hosttest_arc.c: L1 is an array, the NIU registers are
 * an array, and writing command-buffer register 0x40 calls the fake NoC. */
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

#define TXID 0xeu
#define SPIN_MAX 4000000u

struct ctx {
  u32 niu;     /* NIU base for the chosen NoC */
  u32 nb;      /* command buffer base */
  u32 own;
  u32 arc;
  u32 done;    /* reads that completed */
  u32 failed;  /* reads that timed out or were refused */
  u32 dbgn;    /* debug log entries written */
  u32 delay;   /* spin before the copy (probe) */
};

/* Read `words` (<= 12) 32-bit words at `addr` of the ARC tile into dst[]. */
static int arc_read(struct ctx* c, u32 addr, u32 words, u32* dst) {
  u32 spin = 0;
  u32 off = (addr & 15u) >> 2;           /* word offset to match addr % 16 */
  u32 buf = RB + 4u * off;
  u32 i;
  if (words == 0u || words > 12u) {
    ++c->failed;
    return 0;
  }
  while (*NIUX(c->niu, 0x200u + (16u + TXID) * 4u) & 0xffu)
    if (++spin > SPIN_MAX) {
      ++c->failed;
      return 0;
    }
  while (*NIUX(c->nb, 0x40) & 1u)
    if (++spin > SPIN_MAX) {
      ++c->failed;
      return 0;
    }
  for (i = 0; i < words; ++i) *L1(buf + 4u * i) = 0xDEADBEEFu;
  *NIUX(c->nb, 0x00) = addr;            /* target address low */
  *NIUX(c->nb, 0x04) = 0;
  *NIUX(c->nb, 0x08) = c->arc;          /* target node */
  *NIUX(c->nb, 0x0c) = buf;             /* return address: our L1 */
  *NIUX(c->nb, 0x10) = 0;
  *NIUX(c->nb, 0x14) = c->own;          /* return node: this tile */
  *NIUX(c->nb, 0x18) = TXID << 10;      /* packet tag: transaction id */
  *NIUX(c->nb, 0x1c) = 0u;              /* NOC_CMD_RD */
  *NIUX(c->nb, 0x20) = words << 2;      /* byte count */
  *NIUX(c->nb, 0x24) = 0;
  *NIUX(c->nb, 0x40) = 1u;              /* go */
#ifdef HOSTTEST
  ht_noc_go(c->nb);
#endif
  { volatile u32 rb = *NIUX(c->nb, 0x40); (void)rb; }  /* ordering read */
  while (*NIUX(c->niu, 0x200u + (16u + TXID) * 4u) & 0xffu)
    if (++spin > SPIN_MAX) {
      ++c->failed;
      return 0;
    }
  if (c->delay) {
    volatile u32 d;
    for (d = 0; d < c->delay; ++d) {}
  }
  for (i = 0; i < words; ++i) dst[i] = *L1(buf + 4u * i);
  if (c->dbgn < 8u) {
    u32 e = CT(0x300u + 0x48u * c->dbgn);
    *L1(e) = addr;
    *L1(e + 4u) = buf;
    for (i = 0; i < 16u; ++i) *L1(e + 8u + 4u * i) = *L1(RB + 4u * i);
    ++c->dbgn;
  }
  ++c->done;
  return 1;
}

static int plausible(u32 p) {
  return p != 0u && p != 0xFFFFFFFFu && (p & 3u) == 0u;
}

static void pass(struct ctx* c) {
  u32 w[12];
  u32 st = 0;
  u32 boot = 0, qinfo = 0, tdata = 0, tptr = 0, ver = 0, cnt = 0;
  u32 aiclk = 0, axiclk = 0, arcclk = 0, hb = 0, lim = 0, temp = 0;
  u32 pwr = 0, vcore = 0, l2c = 0xFFFFFFFFu, aoff = 0xFFFFu;
  u32 offs[9];
  u32 ids[9];
  u32 nids = 9u;
  u32 i, n, e, k;
  c->done = 0;
  c->failed = 0;
  ids[0] = 14u;  ids[1] = 15u;  ids[2] = 16u;  ids[3] = 32u;
  ids[4] = 63u;  ids[5] = 11u;  ids[6] = 7u;   ids[7] = 6u;
  ids[8] = 17u;
  for (i = 0; i < nids; ++i) offs[i] = 0xFFFFFFFFu;
  if (*L1(CT(0x24))) {
    u32 n;
    u32 pw[12];
    c->dbgn = 0;
    for (n = 0; n < 16u; ++n) {
      pw[0] = 0xEEEEEEEEu;
      (void)arc_read(c, 0x80030400u + 4u * n, 1u, pw);
      *L1(CT(0x600u + 4u * n)) = pw[0];
    }
    c->delay = 4000u;
    for (n = 0; n < 16u; ++n) {
      pw[0] = 0xEEEEEEEEu;
      (void)arc_read(c, 0x80030400u + 4u * n, 1u, pw);
      *L1(CT(0x640u + 4u * n)) = pw[0];
    }
    c->delay = 0;
    for (n = 0; n < 12u; ++n) pw[n] = 0xEEEEEEEEu;
    (void)arc_read(c, 0x80030400u, 12u, pw);
    for (n = 0; n < 12u; ++n) *L1(CT(0x680u + 4u * n)) = pw[n];
    for (n = 0; n < 12u; ++n) pw[n] = 0xEEEEEEEEu;
    (void)arc_read(c, 0x80030410u, 12u, pw);
    for (n = 0; n < 12u; ++n) *L1(CT(0x6B0u + 4u * n)) = pw[n];
    for (n = 0; n < 12u; ++n) pw[n] = 0xEEEEEEEEu;
    (void)arc_read(c, 0x80030430u, 12u, pw);
    for (n = 0; n < 12u; ++n) *L1(CT(0x6E0u + 4u * n)) = pw[n];
    for (n = 0; n < 12u; ++n) pw[n] = 0xEEEEEEEEu;
    (void)arc_read(c, 0x1001a094u, 2u, pw);
    for (n = 0; n < 2u; ++n) *L1(CT(0x710u + 4u * n)) = pw[n];
    for (n = 0; n < 12u; ++n) pw[n] = 0xEEEEEEEEu;
    (void)arc_read(c, 0x1001a1a0u, 12u, pw);
    for (n = 0; n < 12u; ++n) *L1(CT(0x720u + 4u * n)) = pw[n];
    for (n = 0; n < 12u; ++n) pw[n] = 0xEEEEEEEEu;
    (void)arc_read(c, 0x80030430u, 4u, pw);
    for (n = 0; n < 4u; ++n) *L1(CT(0x750u + 4u * n)) = pw[n];
    for (n = 0; n < 12u; ++n) pw[n] = 0xEEEEEEEEu;
    (void)arc_read(c, 0x8003042cu, 2u, pw);
    for (n = 0; n < 2u; ++n) *L1(CT(0x760u + 4u * n)) = pw[n];
    c->done = 0;
    c->failed = 0;
  } else {
    c->dbgn = 0;
  }
  if (!arc_read(c, 0x80030400u + 4u * 2u, 1u, w)) { st = 2u; goto out; }
  boot = w[0];
  if (!arc_read(c, 0x80030400u + 4u * 11u, 1u, w)) { st = 2u; goto out; }
  qinfo = w[0];
  if (!arc_read(c, 0x80030400u + 4u * 12u, 1u, w)) { st = 2u; goto out; }
  tdata = w[0];
  if (!arc_read(c, 0x80030400u + 4u * 13u, 1u, w)) { st = 2u; goto out; }
  tptr = w[0];
  if (!plausible(tptr) || !plausible(tdata)) { st = 1u; goto out; }
  if (!arc_read(c, tptr, 2u, w)) { st = 2u; goto out; }
  ver = w[0];
  cnt = w[1];
  if (cnt == 0u || cnt > 128u) { st = 3u; goto out; }
  for (e = 0; e < cnt; e += 12u) {
    n = cnt - e;
    if (n > 12u) n = 12u;
    if (!arc_read(c, tptr + 8u + 4u * e, n, w)) { st = 2u; goto out; }
    for (k = 0; k < n; ++k) {
      u32 id = w[k] & 0xffffu;
      for (i = 0; i < nids; ++i)
        if (ids[i] == id) offs[i] = w[k] >> 16;
    }
  }
  if (offs[0] == 0xFFFFFFFFu) { st = 4u; goto out; }
  aoff = offs[0];
  for (i = 0; i < nids; ++i) {
    u32 v = 0;
    if (offs[i] == 0xFFFFFFFFu) continue;
    if (!arc_read(c, tdata + 4u * offs[i], 1u, w)) { st = 2u; goto out; }
    v = w[0];
    if (i == 0u) aiclk = v;
    else if (i == 1u) axiclk = v;
    else if (i == 2u) arcclk = v;
    else if (i == 3u) hb = v;
    else if (i == 4u) lim = v;
    else if (i == 5u) temp = v;
    else if (i == 6u) pwr = v;
    else if (i == 7u) vcore = v;
    else if (i == 8u) l2c = v;
  }
out:
  *L1(CT(0x10C)) = boot;
  *L1(CT(0x110)) = qinfo;
  *L1(CT(0x114)) = tdata;
  *L1(CT(0x118)) = tptr;
  *L1(CT(0x11C)) = ver;
  *L1(CT(0x120)) = cnt;
  *L1(CT(0x124)) = aiclk;
  *L1(CT(0x128)) = axiclk;
  *L1(CT(0x12C)) = arcclk;
  *L1(CT(0x130)) = hb;
  *L1(CT(0x134)) = lim;
  *L1(CT(0x138)) = temp;
  *L1(CT(0x13C)) = pwr;
  *L1(CT(0x140)) = vcore;
  *L1(CT(0x144)) = aoff;
  *L1(CT(0x148)) = l2c;
  *L1(CT(0x104)) = c->done;
  *L1(CT(0x108)) = c->failed;
  *L1(CT(0x100)) = st;
}

#ifdef HOSTTEST
void ht_pass(u32 noc, u32 own, u32 arc) {
  struct ctx c;
  c.niu = noc ? 0xFFB30000u : 0xFFB20000u;
  c.nb = c.niu + 3u * 0x800u;
  c.own = own;
  c.arc = arc;
  c.done = 0;
  c.failed = 0;
  c.dbgn = 0;
  c.delay = 0;
  pass(&c);
}
#else
static void run(void) {
  struct ctx c;
  u32 noc = *L1(CT(0x14)) & 1u;
  u32 buf = *L1(CT(0x20));
  if (buf == 0u) buf = 3u;
  if (buf == 0xFFu) buf = 0u;
  if (buf > 3u) buf = 3u;
  c.niu = noc ? 0xFFB30000u : 0xFFB20000u;
  c.nb = c.niu + buf * 0x800u;
  c.own = *L1(CT(0x18));
  c.arc = *L1(CT(0x1C));
  c.done = 0;
  c.failed = 0;
  c.dbgn = 0;
  c.delay = 0;
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
  *L1(CT(0x3C)) = T6ARC_ID;
  *L1(CT(0x08)) = 0x7E57A11Eu;
  run();
  *L1(CT(0x0C)) = 2;
  for (;;) {}
}

/* entry table as in _BUD.S / t6pp3.c: word 0 = BRISC (reset address 0),
 * word 4 (address 16) = NCRISC when the host sets its reset-PC override. */
__asm__(".section .text.start,\"ax\"\n.globl _start\n_start:\n  j start_b\n"
        "  j hang\n  j hang\n  j hang\n  j start_nc\n"
        "hang: j hang\n"
        "start_b:\n  li sp, 0xFFB02000\n  call main_t6\n1: j 1b\n"
        "start_nc:\n  li sp, 0xFFB02000\n  call main_t6\n2: j 2b\n.previous\n");
#endif

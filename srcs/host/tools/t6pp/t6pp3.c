/* t6pp3.c -- t6pp2.c plus: the NoC (NIU0 or NIU1) the image drives for its writes to the ETH tile is a control word (+0x4C: 0 = NIU0 at
 * 0xFFB20000, the default and what every earlier image used; 1 = NIU1 at 0xFFB30000), so the T6 -> ERISC write can be tried over NoC1
 * (expts15.sh, ethfwd21 --t6-noc).  Node ids stay raw NoC0 coordinates, as MFMx does on NIU1.  Image id 0x76500003.  New file; t6pp2.c is
 * unchanged.  Why: NRI3::initiateWriteToEthL1 is forced to NoC0 because NoC1 -> ETH tile was never exercised on hardware.
 *
 * ---- t6pp2.c header follows ----
 * t6pp2.c -- t6pp.c plus: (a) entry points for BOTH BRISC (address 0) and NCRISC (address 16, via the NCRISC reset-PC override, as
 * srcs/host/src/OurTLBs.cpp resetTheFleet / _BUD.S do), so the same image can run on either hart; (b) the NIU command-buffer index is a
 * control word (+0x48, default 3) instead of a constant.  Image id 0x76500002.  New file; t6pp.c is unchanged.
 *   Why: MFMx reserves NRI3 (cmd buf 3) for hart NC by software convention (NRIUtils.cpp asserts it).  Which harts may drive which NIU command
 *   buffers in hardware is not established here, so the host tool can start the image on NC (like MFMx) or on B, and with any cmd buffer 0..3.
 *
 * ---- t6pp.c header follows ----
 * t6pp.c -- bare-metal Tensix BRISC "car ping-pong" image for the eth-seam (stage 3a): replaces the host in car management.
 * New file (srcs/host/tools/t6pp/).  Built by t6pp/build.sh with the sfpi gcc toolchain, linked at T6 L1 address 0 (BRISC's fixed
 * reset address), runs ONLY on hart B; the host tool (ethfwd17 --pp) holds the tile's other cores in reset.  Startup mirrors the
 * bits of srcs/cross/src/_BUD.S that matter for one hart: sp = top of the 8 KiB local RAM (0xFFB02000), CSR 0x7c0 bit 3 (DisLowCash)
 * to switch the L0 data cache off (exactly what _BUD.S does on every hart), no C++ ctors, no .data/.bss (state lives on the stack).
 *
 * What it does: one end of a car ping-pong across the Ethernet seam.  Cars live in this tile's L1 RECEIVE rings (one ring of NR cars
 * per channel).  The local E1 (e1fwd13/14/15, ROUTER with legs L_OUT|L_IN_NOC) NoC-writes cars arriving over the link into these rings
 * (slot i of channel c at RCV0 + c*RSTRIDE + i*CAR) and ships whatever this image NoC-writes into its OUT rings (E1 L1 at
 * E1OUT0 + c*OSTRIDE + i*CAR, node E1NODE).  Slot index i is preserved end to end.
 *   role 0 (initiator): seeds NR cars per channel (nonce 1, state 3, payload payload_word(1,w,(c<<2)|i)), then behaves as role 1.
 *   role 1 (reflector): for each NEW COMPLETE car in a receive slot (header == ankle == footer, header != last handled for the slot)
 *       optionally verifies its payload (every VE-th car), flips it in place (nonce+1, state 3<->4, ankle, footer, header written in that
 *       order), reads the header back (dependency token into the NoC SRC register) and NoC-writes the slot to the E1's OUT slot.
 * Each channel is served in slot order, at most NR cars per visit (same fairness bound as the E1).  Car rules: shared/include/TCMarker.h.
 *
 * Control block at CTLB (default 0x8000, T6 L1), window 0x400, written by the host BEFORE the core is released except go:
 *   +00 go  +04 stop  +08 ready (0x7E57A11E written by the image)  +0C done  +10 role  +14 nch (1..8)  +18 nr (1..4)  +1C verify-every (0=off)
 *   +20 tcms  +24 e1node (y<<6|x of the E1's ETH tile)  +28 ownnode (y<<6|x of THIS tile, NoC0)  +2C e1out0  +30 ostride  +34 rcv0
 *   +38 rstride  +3C image id  +40 sweeps  +44 csr readback (|0x80000000)  +48 NIU cmd buffer (0 = default 3, 0xFF = buffer 0, 1..3 as is)  +4C NoC (0 = NIU0, 1 = NIU1)
 *   per-channel stats at +100 + c*0x40: 0 handled (cars received & reflected)  1 bad payload  2 verify checks  3 noc-fail
 *   4 partial sightings  5 first bad word  6 got  7 expected  8 next slot index
 * NoC command sequence = NRI3::initiateWriteToT6 (NIU0 cmd buf 3), proven on the E1 (e1fwd*.c noc_write) and on ETH tiles.
 */
typedef unsigned int u32;
#define PP_ID 0x76500003u
#ifndef CTLB
#define CTLB 0x8000u
#endif
#ifndef CAR
#define CAR 0x1040u
#endif
#define NCH_MAX 8u
#define NR_MAX 4u
#define MAX_TCMS 66u

#ifdef HOSTTEST
extern __thread char* g_l1;
extern void ht_noc_cmd(void);
extern __thread u32 g_nri[0x20];
#define L1(a) ((volatile u32*)(g_l1 + (a)))
#define NRI(base, off) ((void)(base), (volatile u32*)&g_nri[(off) / 4])
static inline void csr_set_l0off(void) {}
static inline u32 csr_read_l0(void) { return 8u; }
static inline u32 dep0(u32 v) { u32 z = 0; __asm__ volatile("" : "+r"(z) : "r"(v)); return z; }
#else
typedef unsigned long uintptr_t_;
#define L1(a) ((volatile u32*)(uintptr_t_)(a))
#define NRI(base, off) ((volatile u32*)(uintptr_t_)((base) + (off)))
static inline void csr_set_l0off(void) { u32 v = 8u; __asm__ volatile("csrrs x0, 0x7c0, %0" ::"r"(v)); }
static inline u32 csr_read_l0(void) { u32 v; __asm__ volatile("csrr %0, 0x7c0" : "=r"(v)); return v; }
static inline u32 dep0(u32 v) { u32 z; __asm__ volatile("and %0, %1, zero" : "=r"(z) : "r"(v)); return z; }
#endif

#define CT(o) (CTLB + (o))
#define ST(c, k) (CTLB + 0x100u + (c) * 0x40u + 4u * (k))
enum { P_HANDLED = 0, P_BAD = 1, P_VCHK = 2, P_NOCFAIL = 3, P_PARTIAL = 4, P_VWORD = 5, P_VGOT = 6, P_VEXP = 7, P_IDX = 8 };

static inline u32 pkt_bytes(u32 tcms) { return tcms == 0u ? 16u : (tcms == 1u ? 32u : (tcms - 1u) * 64u); }
static inline u32 mk_hdr(u32 nonce, u32 tcms, u32 state) { return 0x2cu | ((nonce & 0xffu) << 8) | (tcms << 16) | (state << 24); }
static inline u32 payload_word(u32 n, u32 w, u32 salt) { return (n * 0x01010101u) ^ (w * 0x9E3779B1u) ^ (salt * 0x00010001u); }
static inline u32 nxt(u32 i, u32 nr) { return (i + 1u >= nr) ? 0u : i + 1u; }

/* complete car at slot?  bytes (>0) and header, else 0; *partial = marker present but not complete */
static inline u32 car_check(u32 slot, u32* hdr, u32* partial) {
  u32 h = *L1(slot);
  *partial = 0;
  if ((h & 0xffu) != 0x2cu) return 0;
  u32 st = h >> 24;
  if (st != 3u && st != 4u) return 0;
  u32 tcms = (h >> 16) & 0xffu;
  if (tcms > MAX_TCMS) return 0;
  u32 bytes = pkt_bytes(tcms);
  if (*L1(slot + bytes - 4u) != h || (tcms >= 2u && *L1(slot + bytes - 20u) != h)) { *partial = 1; return 0; }
  *hdr = h;
  return bytes;
}

/* NoC write of [src,src+bytes) from this tile's L1 to (node, dst): NRI3::initiateWriteToT6.  src%16 == dst%16.  dtok orders prior L1 stores. */
static int noc_write(u32 nb, u32 srcnode, u32 dtok, u32 src, u32 node, u32 dst, u32 bytes) {
  u32 spin = 0;
  while (*NRI(nb, 0x40) & 1u) if (++spin > 4000000u) return 0;
  *NRI(nb, 0x00) = src + dep0(dtok);  *NRI(nb, 0x04) = 0;  *NRI(nb, 0x08) = srcnode;
  *NRI(nb, 0x0c) = dst;  *NRI(nb, 0x10) = 0;  *NRI(nb, 0x14) = node;
  *NRI(nb, 0x18) = 0;    *NRI(nb, 0x1c) = 2u;
  *NRI(nb, 0x20) = bytes; *NRI(nb, 0x24) = 0;
  *NRI(nb, 0x40) = 1u;
#ifdef HOSTTEST
  ht_noc_cmd();
#endif
  { volatile u32 rb = *NRI(nb, 0x40); (void)rb; }
  return 1;
}

struct pp {
  u32 last[NCH_MAX][NR_MAX];
  u32 idx[NCH_MAX];
  u32 st[NCH_MAX][9];
  u32 pmask;
  u32 role, nch, nr, ve, tcms, e1node, own, out0, ostr, rcv0, rstr, vcount, nb;
};

static inline void st_add(struct pp* p, u32 c, u32 k, u32 by) { u32 v = (p->st[c][k] += by); *L1(ST(c, k)) = v; }
static inline void st_set(struct pp* p, u32 c, u32 k, u32 v) { p->st[c][k] = v; *L1(ST(c, k)) = v; }

static void seed(struct pp* p) {
  u32 tcms = p->tcms, bytes = pkt_bytes(tcms), words = bytes / 4u, fi = words - 1u, ai = tcms >= 2u ? words - 5u : fi;
  for (u32 c = 0; c < p->nch; ++c)
    for (u32 i = 0; i < p->nr; ++i) {
      u32 slot = p->rcv0 + c * p->rstr + i * CAR, salt = (c << 2) | i, h = mk_hdr(1u, tcms, 3u);
      for (u32 w = 1; w < words; ++w) if (w != fi && w != ai) *L1(slot + 4u * w) = payload_word(1u, w, salt);
      if (tcms >= 2u) *L1(slot + 4u * ai) = h;
      *L1(slot + 4u * fi) = h;
      *L1(slot) = h;
      u32 rb = *L1(slot);
      if (!noc_write(p->nb, p->own, rb, slot, p->e1node, p->out0 + c * p->ostr + i * CAR, bytes)) st_add(p, c, P_NOCFAIL, 1u);
      p->last[c][i] = h;
    }
}

static void note_partial(struct pp* p, u32 c, u32 i) {
  u32 bit = 1u << (c * 4u + i);
  if (!(p->pmask & bit)) { p->pmask |= bit; st_add(p, c, P_PARTIAL, 1u); }
}

static void leg(struct pp* p, u32 c) {
  for (u32 n = 0; n < p->nr; ++n) {
    u32 i = p->idx[c], slot = p->rcv0 + c * p->rstr + i * CAR, h, part;
    u32 bytes = car_check(slot, &h, &part);
    if (!bytes) { if (part) note_partial(p, c, i); break; }
    if (h == p->last[c][i]) break;
    p->pmask &= ~(1u << (c * 4u + i));
    u32 tcms = (h >> 16) & 0xffu, st = h >> 24, words = bytes / 4u, fi = words - 1u, ai = tcms >= 2u ? words - 5u : fi, salt = (c << 2) | i;
    st_add(p, c, P_HANDLED, 1u);
    if (p->ve && ++p->vcount >= p->ve) {
      p->vcount = 0;
      u32 bad = 0, bw = 0, bgot = 0, bexp = 0;
      for (u32 w = 1; w < words; ++w) {
        if (w == fi || w == ai) continue;
        u32 got = *L1(slot + 4u * w), exp = payload_word(1u, w, salt);
        if (got != exp) { bw = w; bgot = got; bexp = exp; bad = 1; break; }
      }
      st_add(p, c, P_VCHK, 1u);
      if (bad) {
        if (p->st[c][P_BAD] == 0) { st_set(p, c, P_VWORD, bw); st_set(p, c, P_VGOT, bgot); st_set(p, c, P_VEXP, bexp); }
        st_add(p, c, P_BAD, 1u);
      }
    }
    u32 n2 = ((h >> 8) + 1u) & 0xffu, h2 = mk_hdr(n2, tcms, 7u - st);
    if (tcms >= 2u) *L1(slot + 4u * ai) = h2;
    *L1(slot + 4u * fi) = h2;
    *L1(slot) = h2;
    u32 rb = *L1(slot);
    if (!noc_write(p->nb, p->own, rb, slot, p->e1node, p->out0 + c * p->ostr + i * CAR, bytes)) { st_add(p, c, P_NOCFAIL, 1u); break; }
    p->last[c][i] = h2; p->idx[c] = nxt(i, p->nr);
    st_set(p, c, P_IDX, p->idx[c]);
  }
}

#ifdef HOSTTEST
void pp_run(void)
#else
static void pp_run(void)
#endif
{
  struct pp p;
  p.role = *L1(CT(0x10)); p.nch = *L1(CT(0x14)); p.nr = *L1(CT(0x18)); p.ve = *L1(CT(0x1c)); p.tcms = *L1(CT(0x20));
  p.e1node = *L1(CT(0x24)); p.own = *L1(CT(0x28)); p.out0 = *L1(CT(0x2c)); p.ostr = *L1(CT(0x30)); p.rcv0 = *L1(CT(0x34)); p.rstr = *L1(CT(0x38));
  { u32 buf = *L1(CT(0x48)); if (buf == 0u) buf = 3u; if (buf == 0xFFu) buf = 0u; if (buf > 3u) buf = 3u; p.nb = ((*L1(CT(0x4c)) & 1u) ? 0xFFB30000u : 0xFFB20000u) + buf * 0x800u; }   /* +0x4C: 0 NIU0, 1 NIU1 */   /* +0x48: 0 -> default 3, 0xFF -> buffer 0 */
  if (p.nch < 1u || p.nch > NCH_MAX) p.nch = 1u;
  if (p.nr < 1u || p.nr > NR_MAX) p.nr = NR_MAX;
  if (p.tcms > MAX_TCMS) p.tcms = 2u;
  p.vcount = 0; p.pmask = 0;
  for (u32 c = 0; c < NCH_MAX; ++c) {
    p.idx[c] = 0;
    for (u32 i = 0; i < NR_MAX; ++i) p.last[c][i] = 0;
    for (u32 k = 0; k < 9u; ++k) { p.st[c][k] = 0; if (c < p.nch) *L1(ST(c, k)) = 0; }
  }
  *L1(CT(0x44)) = 0u;
  csr_set_l0off();
  *L1(CT(0x44)) = csr_read_l0() | 0x80000000u;
  if (p.role == 0u) seed(&p);
  u32 sweeps = 0;
  for (;;) {
    if ((sweeps & 15u) == 0u && *L1(CT(0x04))) break;
    for (u32 c = 0; c < p.nch; ++c) leg(&p, c);
    *L1(CT(0x40)) = ++sweeps;
  }
}

#ifndef HOSTTEST
void main_t6(void) {
  *L1(CT(0x3c)) = PP_ID;
  *L1(CT(0x08)) = 0x7E57A11Eu;
  for (;;) {
    while (!*L1(CT(0x00))) {}
    *L1(CT(0x0c)) = 0;
    pp_run();
    *L1(CT(0x0c)) = 1;
    while (*L1(CT(0x00))) {}
  }
}
/* entry table as in _BUD.S: word 0 = BRISC (fixed reset address 0), words 1-3 = TRISC0-2 entries (not used here), word 4 (address 16) = NCRISC
 * when the host sets NCRISC_RESET_PC=16 and its override.  Both live harts have 8 KiB of local RAM, so one stack top serves both. */
__asm__(".section .text.start,\"ax\"\n.globl _start\n_start:\n  j start_b\n  j hang\n  j hang\n  j hang\n  j start_nc\n"
        "hang: j hang\n"
        "start_b:\n  li sp, 0xFFB02000\n  call main_t6\n1: j 1b\n"
        "start_nc:\n  li sp, 0xFFB02000\n  call main_t6\n2: j 2b\n");
#endif

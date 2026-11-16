/* e1fwd14.c -- e1fwd13 with the hot-loop state moved into E1's local RAM (stack).  New file; versions are never reused.
 *
 * Differences from e1fwd13 (logic, wire format and CTL/channel layout are unchanged, so ethfwd14 can drive either):
 *   - all per-channel CONFIG words (OUT/IN/PEERIN/T6NODE/T6BASE/LEGS) and this tile's NoC node id are copied from the L1 CTL
 *     block into a struct on the stack ONCE at run start.  The host must therefore write them BEFORE raising go (it already
 *     does); changing them while running has no effect.
 *   - all STATS counters live on the stack and are PUBLISHED to the L1 stat words with a plain store on every change (no
 *     L1 read-modify-write).  They are zeroed (locally and in L1) at run start.
 *   - the stop word is polled every STOP_EVERY (16) sweeps, not every sweep.  The sweep counter (CTL+0x30) is still stored
 *     every sweep.
 *   Rationale (ERISC_NOTES sec 3/8, hw run of the gcc e1fwd11): an L1 load with the L0 off costs ~26 cycles at 800 MHz, a
 *   local-RAM load ~2.  Only ring slot words, the stop word and the host-visible stats remain in L1.  Everything below not
 *   mentioned here is e1fwd13 as it was.
 *
 * ---- e1fwd13 header follows ----
 * e1fwd13.c -- Blackhole Ethernet-tile E1 image: bidirectional MFMx car router for the eth-seam (see
 * ERISC_SEAM_DESIGN.md).
 *
 * Differences from e1fwd11 (whose proven pieces -- car completeness test, chunked TXQ2 send, NIU0 cmd buf 3 NoC write,
 * fence/L0 handling -- are kept):
 *   - up to NCH_MAX (8) channels, each with its OWN config: OUT ring, IN ring, peer's IN ring, local T6 node + T6 ring
 *     base, and a LEGS word choosing what this E1 does for the channel.  (e1fwd11 had one global T6 node/base and mode 7
 *     served channel 0 only.)
 *   - ONE role (M_ROUTER) runs both legs on every enabled channel in the same sweep:
 *       L_OUT    : a new complete car in OUT[c][i] (written by the local T6 over the NoC) is sent over TXQ2 to the
 *                  peer E1's IN[c][i]  (peer address = C_PEERIN + i*CAR).
 *       L_IN_NOC : a new complete car in IN[c][i] (written by the peer E1 over the link) is NoC-written to the local T6
 *                  at (C_T6NODE, C_T6BASE + i*CAR).
 *       L_IN_COUNT: count arrivals on IN[c] only (host plays the hubs, as in stage 1a).
 *   - each leg visit forwards at most one ring's worth of cars, so no channel can starve the others.
 *   - extra diagnostics: S_PARTIAL counts slots in which a car marker was seen but the car was not (yet) complete
 *     (once per slot occupancy, not per poll); useful for the two-streams ordering question.
 *   - CTL_BASE can be overridden at build time (-DCTL_BASE=...) to relocate above tt-metal's reserved region later.
 *   - an image id word (CTL+0x54 = IMAGE_ID) lets the host confirm which image is running.
 *   - the stage-0 L0 polling modes and the stage-2c NOCLOOP generator are not carried over (use e1fwd11 for those).
 *
 * Modes (CTL+0x10): 5 = M_ROUTER, 6 = M_REFLECT (bounce IN-ring cars straight back, optional regen/verify; link tests).
 *
 * CAR RULES (shared/include/TCMarker.h, TCBase.h): marker word = 0x2C | nonce<<8 | tcms<<16 | state<<24; packet bytes
 * = 16 (tcms 0), 32 (tcms 1), (tcms-1)*64 (tcms>=2); header = word 0, footer = last word, ankle = word (words-5)
 * when tcms>=2; a car is complete iff header == ankle == footer.  A car is NEW for a slot iff complete and its header
 * differs from the last one handled for that slot (TCBase::setDepartingTC bumps the nonce on every departure).  Cars are
 * owned by the far end after shipping until a complete car with a changed header arrives; slot order is index order
 * mod RING cars.  The host must ZERO the rings before release: per-slot "last header" state lives on the stack and starts
 * at 0, and L1 is not zeroed at boot, so a stale complete car would be forwarded as new.
 *
 * E1 writes ONLY: its CTL/stat block, car rings (REFLECT regen), the TXQ2 registers SRC(+0x14) SIZE(+0x18) DEST(+0x1C)
 * CMD(+0x04, value 2), NIU0 cmd buf 3 registers (as MFMx NRI3::initiateWriteToT6) and, in F_CSR, CSR 0x7c0.
 * Everything else (queue 2 config, TX header entry, E1 reset) is done by the host.
 *
 * Memory ordering: the L0 data cache is non-coherent; this image FENCEs before polling unless the CSR route (F_CSR,
 * DisLowCache) was selected.  Stores to L1 followed by a TXQ CMD store need a read-back dependency.
 *
 * L1 map (defaults; all host-chosen except the three below): image at 0x4000 (linker script), CTL block at CTL_BASE
 * (0x8000) of size 0x600: global words at CTL+0x00..0x5F, channel blocks at CTL+0x100 + c*0x80.
 */
typedef unsigned int u32;
#define IMAGE_ID 0xE1F00014u
#define STOP_EVERY 16u
#ifdef HOSTTEST
extern __thread char* g_l1;
extern __thread u32 g_txq[0x40];
extern void ht_txq_cmd(void);
#define L1(a) ((volatile u32*)(g_l1 + (a)))
#define TXR(off) ((volatile u32*)&g_txq[(off) / 4])
#define FENCE() __asm__ volatile("" ::: "memory")
extern __thread u32 g_nri[0x20];
extern void ht_noc_cmd(void);
#define NRI(off) ((volatile u32*)&g_nri[(off) / 4])
static inline void csr_set_l0off(void) {}
static inline u32 csr_read_l0(void) { return 8u; }
static inline u32 dep0(u32 v) { u32 z = 0; __asm__ volatile("" : "+r"(z) : "r"(v)); return z; }
#else
typedef unsigned long uintptr_t_;
#define L1(a) ((volatile u32*)(uintptr_t_)(a))
#define TXR(off) ((volatile u32*)(uintptr_t_)(0xFFB92000u + (off)))
#define FENCE() __asm__ volatile("fence" ::: "memory")
#define NRI(off) ((volatile u32*)(uintptr_t_)(0xFFB20000u + 3u * 0x800u + (off)))
static inline void csr_set_l0off(void) { u32 v = 8u; __asm__ volatile("csrrs x0, 0x7c0, %0" ::"r"(v)); }
static inline u32 csr_read_l0(void) { u32 v; __asm__ volatile("csrr %0, 0x7c0" : "=r"(v)); return v; }
static inline u32 dep0(u32 v) { u32 z; __asm__ volatile("and %0, %1, zero" : "=r"(z) : "r"(v)); return z; }
#endif

#ifndef CTL_BASE
#define CTL_BASE 0x8000u
#endif
#define CTL CTL_BASE
#define CHC (CTL_BASE + 0x100u)          /* channel blocks: CHC + c*0x80 */
#define CHB(c) (CHC + (c) * 0x80u)
#define NCH_MAX 8u
#define CAR 0x1040u                      /* 4160 B: InterHubBlock */
#define RING_CARS_MAX 4u
#define CHUNK 1456u                      /* payload bytes per TT-link packet: 1456+32 <= 1500, multiple of 16 */
#define MAX_TCMS 66u

enum { M_ROUTER = 5, M_REFLECT = 6 };
enum { F_FENCE = 1, F_CSR = 2, F_REGEN = 4 };
/* leg bits in the channel LEGS word */
enum { L_OUT = 1, L_IN_NOC = 2, L_IN_COUNT = 4 };
/* CTL words: +0 go, +4 stop, +8 ready, +C done, +10 mode, +18 flags, +1C nch (<=8), +20 verify-every (reflect),
 *            +24 ring cars (1..4, default 4), +28 csr marker, +2C csr readback, +30 sweeps, +44 this tile's NoC node id
 *            (y<<6|x), +54 image id.
 * channel block words (u32 index): config 0..7, stats 8..23. */
enum { C_OUT = 0, C_IN = 1, C_PEERIN = 2, C_T6NODE = 3, C_T6BASE = 4, C_LEGS = 5,
       S_FWD = 8, S_INSEEN = 9, S_REFL = 10, S_TXFAIL = 11, S_VCHK = 12, S_VBAD = 13, S_VWORD = 14, S_VGOT = 15,
       S_VEXP = 16, S_OUTIDX = 17, S_ININDEX = 18, S_BYTES = 19, S_NOCW = 20, S_NOCFAIL = 21, S_PARTIAL = 22,
       S_NOCBYTES = 23 };

static inline u32 pkt_bytes(u32 tcms) { return tcms == 0u ? 16u : (tcms == 1u ? 32u : (tcms - 1u) * 64u); }
static inline u32 mk_hdr(u32 nonce, u32 tcms, u32 state) { return 0x2cu | ((nonce & 0xffu) << 8) | (tcms << 16) | (state << 24); }
static inline u32 payload_word(u32 n, u32 w, u32 salt) { return (n * 0x01010101u) ^ (w * 0x9E3779B1u) ^ (salt * 0x00010001u); }
static inline u32 nxt(u32 i, u32 nr) { return (i + 1u >= nr) ? 0u : i + 1u; }

/* complete car at slot?  Returns packet bytes (>0) and the header word, else 0.  *partial = 1 if a car marker is present
 * but the car is not complete (in flight, or torn). */
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

/* send [src, src+bytes) to the peer's L1 at dst via TXQ2 in <= CHUNK pieces; dtok = dependency token (see header) */
static int tx_send(u32 src, u32 dst, u32 bytes, u32 dtok) {
  u32 off = 0, first = 1;
  while (off < bytes) {
    u32 n = bytes - off;
    if (n > CHUNK) n = CHUNK;
    u32 spin = 0;
    while (*TXR(0x08) & 0x10000u) if (++spin > 400000u) return 0;
    *TXR(0x14u + (first ? dep0(dtok) : 0u)) = src + off;
    first = 0;
    *TXR(0x1c) = dst + off;
    *TXR(0x18) = n;
    *TXR(0x04) = 2u;
#ifdef HOSTTEST
    ht_txq_cmd();
#endif
    off += n;
  }
  return 1;
}

/* NoC write of [src,src+bytes) from this tile's L1 to (node, dst) -- mirrors NRI3::initiateWriteToT6.  src%16 == dst%16. */
static int noc_write(u32 srcnode, u32 src, u32 node, u32 dst, u32 bytes) {
  u32 spin = 0;
  while (*NRI(0x40) & 1u) if (++spin > 400000u) return 0;
  *NRI(0x00) = src;  *NRI(0x04) = 0;  *NRI(0x08) = srcnode;   /* source: this tile's node id (y<<6|x) */
  *NRI(0x0c) = dst;  *NRI(0x10) = 0;  *NRI(0x14) = node;
  *NRI(0x18) = 0;    *NRI(0x1c) = 2u;                                   /* no overlay delivery; NOC_CMD_WR */
  *NRI(0x20) = bytes; *NRI(0x24) = 0;
  *NRI(0x40) = 1u;
#ifdef HOSTTEST
  ht_noc_cmd();
#endif
  { volatile u32 rb = *NRI(0x40); (void)rb; }
  return 1;
}

/* per-run router state; lives on the stack = local RAM (images have no .bss).  Bit (c*4+i) of pmo/pmi = partial already
 * counted.  cfg[] and st[] are local copies of the channel config / stat words (see header). */
struct chcfg { u32 out, in, pin, node, tbase, legs; };
struct rt {
  struct chcfg cfg[NCH_MAX];
  u32 st[NCH_MAX][16];                        /* stat word s of channel c is st[c][s - S_FWD] */
  u32 oidx[NCH_MAX], iidx[NCH_MAX];
  u32 lasto[NCH_MAX][RING_CARS_MAX], lasti[NCH_MAX][RING_CARS_MAX];
  u32 pmo, pmi;
  u32 srcnode;
};

#define ST(r, c, s) ((r)->st[c][(s) - S_FWD])
/* local update + plain-store publish to L1 */
static inline void st_add(struct rt* r, u32 c, u32 s, u32 by) { u32 v = (ST(r, c, s) += by); *L1(CHB(c) + 4u * s) = v; }
static inline void st_set(struct rt* r, u32 c, u32 s, u32 v) { ST(r, c, s) = v; *L1(CHB(c) + 4u * s) = v; }

static void rt_init(struct rt* r, u32 nch) {
  r->srcnode = *L1(CTL + 0x44);
  for (u32 c = 0; c < NCH_MAX; ++c) {
    r->oidx[c] = 0; r->iidx[c] = 0;
    for (u32 i = 0; i < RING_CARS_MAX; ++i) { r->lasto[c][i] = 0; r->lasti[c][i] = 0; }
    for (u32 k = 0; k < 16; ++k) r->st[c][k] = 0;
    r->cfg[c].legs = 0;
    if (c < nch) {
      u32 cb = CHB(c);
      r->cfg[c].out = *L1(cb + 4u * C_OUT);   r->cfg[c].in = *L1(cb + 4u * C_IN);
      r->cfg[c].pin = *L1(cb + 4u * C_PEERIN); r->cfg[c].node = *L1(cb + 4u * C_T6NODE);
      r->cfg[c].tbase = *L1(cb + 4u * C_T6BASE); r->cfg[c].legs = *L1(cb + 4u * C_LEGS);
      for (u32 k = 0; k < 16; ++k) *L1(cb + 4u * (S_FWD + k)) = 0;
    }
  }
  r->pmo = 0; r->pmi = 0;
}

static void note_partial(struct rt* r, u32* mask, u32 c, u32 i) {
  u32 bit = 1u << (c * 4u + i);
  if (!(*mask & bit)) { *mask |= bit; st_add(r, c, S_PARTIAL, 1u); }
}

/* OUT leg: forward new complete cars from OUT[c] to the peer's IN[c] */
static void leg_out(struct rt* r, u32 c, u32 flags, u32 nr) {
  u32 out = r->cfg[c].out, pin = r->cfg[c].pin;
  for (u32 n = 0; n < nr; ++n) {
    if (flags & F_FENCE) FENCE();
    u32 i = r->oidx[c], slot = out + i * CAR, h, part;
    u32 bytes = car_check(slot, &h, &part);
    if (!bytes) { if (part) note_partial(r, &r->pmo, c, i); break; }
    if (h == r->lasto[c][i]) break;
    if (!tx_send(slot, pin + i * CAR, bytes, 0)) { st_add(r, c, S_TXFAIL, 1u); break; }
    r->lasto[c][i] = h; r->oidx[c] = nxt(i, nr);
    r->pmo &= ~(1u << (c * 4u + i));
    st_add(r, c, S_FWD, 1u);
    st_add(r, c, S_BYTES, bytes);
    st_set(r, c, S_OUTIDX, r->oidx[c]);
  }
}

/* IN leg: new complete cars in IN[c] are NoC-written to the local T6 (L_IN_NOC) and/or just counted (L_IN_COUNT) */
static void leg_in(struct rt* r, u32 c, u32 flags, u32 nr) {
  u32 in = r->cfg[c].in, node = r->cfg[c].node, tbase = r->cfg[c].tbase, legs = r->cfg[c].legs;
  for (u32 n = 0; n < nr; ++n) {
    if (flags & F_FENCE) FENCE();
    u32 i = r->iidx[c], slot = in + i * CAR, h, part;
    u32 bytes = car_check(slot, &h, &part);
    if (!bytes) { if (part) note_partial(r, &r->pmi, c, i); break; }
    if (h == r->lasti[c][i]) break;
    if (legs & L_IN_NOC) {
      if (!noc_write(r->srcnode, slot, node, tbase + i * CAR, bytes)) { st_add(r, c, S_NOCFAIL, 1u); break; }
      st_add(r, c, S_NOCW, 1u);
      st_add(r, c, S_NOCBYTES, bytes);
    }
    r->lasti[c][i] = h; r->iidx[c] = nxt(i, nr);
    r->pmi &= ~(1u << (c * 4u + i));
    st_add(r, c, S_INSEEN, 1u);
    st_set(r, c, S_ININDEX, r->iidx[c]);
  }
}

static void role_router(u32 flags, u32 nch, u32 nr) {
  struct rt r;
  rt_init(&r, nch);
  u32 sweeps = 0;
  for (;;) {
    if ((sweeps % STOP_EVERY) == 0u) {
      if (flags & F_FENCE) FENCE();
      if (*L1(CTL + 4)) break;
    }
    for (u32 c = 0; c < nch; ++c) {
      u32 legs = r.cfg[c].legs;
      if (legs & L_OUT) leg_out(&r, c, flags, nr);
      if (legs & (L_IN_NOC | L_IN_COUNT)) leg_in(&r, c, flags, nr);
    }
    *L1(CTL + 0x30) = ++sweeps;
  }
}

/* REFLECT: bounce each new complete IN-ring car straight back to the peer in place (nonce+1, state flipped), optionally
 * regenerating/verifying its payload.  Link+E1 loop tests with no host in the loop. */
static void role_reflect(u32 flags, u32 nch, u32 vevery, u32 nr) {
  struct rt r;
  rt_init(&r, nch);
  u32 sweeps = 0, vcount = 0;
  for (;;) {
    if ((sweeps % STOP_EVERY) == 0u) {
      if (flags & F_FENCE) FENCE();
      if (*L1(CTL + 4)) break;
    }
    for (u32 c = 0; c < nch; ++c) {
      u32 in = r.cfg[c].in, pin = r.cfg[c].pin;
      for (u32 k = 0; k < nr; ++k) {
        if (flags & F_FENCE) FENCE();
        u32 i = r.iidx[c], slot = in + i * CAR, h, part;
        u32 bytes = car_check(slot, &h, &part);
        if (!bytes || h == r.lasti[c][i]) break;
        u32 n = (h >> 8) & 0xffu, tcms = (h >> 16) & 0xffu, st = h >> 24, words = bytes / 4u;
        u32 fi = words - 1u, ai = tcms >= 2u ? words - 5u : fi, salt = (c << 2) | i;
        st_add(&r, c, S_INSEEN, 1u);
        if ((flags & F_REGEN) && vevery && ++vcount >= vevery) {
          vcount = 0;
          u32 bad = 0, bw = 0, bgot = 0, bexp = 0;
          for (u32 w = 1; w < words; ++w) {
            if (w == fi || w == ai) continue;
            u32 got = *L1(slot + 4u * w), exp = payload_word(n, w, salt);
            if (got != exp) { bw = w; bgot = got; bexp = exp; bad = 1; break; }
          }
          st_add(&r, c, S_VCHK, 1u);
          if (bad) {
            if (ST(&r, c, S_VBAD) == 0) { st_set(&r, c, S_VWORD, bw); st_set(&r, c, S_VGOT, bgot); st_set(&r, c, S_VEXP, bexp); }
            st_add(&r, c, S_VBAD, 1u);
          }
        }
        u32 n2 = (n + 1u) & 0xffu, h2 = mk_hdr(n2, tcms, 7u - st);
        if (flags & F_REGEN)
          for (u32 w = 1; w < words; ++w) if (w != fi && w != ai) *L1(slot + 4u * w) = payload_word(n2, w, salt);
        if (tcms >= 2u) *L1(slot + 4u * ai) = h2;
        *L1(slot + 4u * fi) = h2;
        *L1(slot) = h2;
        u32 rb = *L1(slot);                       /* read back: all stores above are processed before the CMD store */
        if (!tx_send(slot, pin + i * CAR, bytes, rb)) { st_add(&r, c, S_TXFAIL, 1u); break; }
        r.lasti[c][i] = h2; r.iidx[c] = nxt(i, nr);
        st_add(&r, c, S_REFL, 1u);
        st_add(&r, c, S_BYTES, bytes);
        st_set(&r, c, S_ININDEX, r.iidx[c]);
      }
    }
    *L1(CTL + 0x30) = ++sweeps;
  }
}

#ifdef HOSTTEST
void e1_run(void)
#else
static void e1_run(void)
#endif
{
  u32 mode = *L1(CTL + 0x10), flags = *L1(CTL + 0x18), nch = *L1(CTL + 0x1c), ve = *L1(CTL + 0x20);
  u32 nr = *L1(CTL + 0x24);
  if (nch > NCH_MAX) nch = NCH_MAX;
  if (nr < 1u || nr > RING_CARS_MAX) nr = RING_CARS_MAX;
  if (flags & F_CSR) {
    *L1(CTL + 0x28) = 0xC5C5C5C5u; csr_set_l0off(); *L1(CTL + 0x2c) = csr_read_l0() | 0x80000000u;
    flags &= ~(u32)F_FENCE;
  }
  if (mode == M_ROUTER) role_router(flags, nch, nr);
  else if (mode == M_REFLECT) role_reflect(flags, nch, ve, nr);
}

#ifndef HOSTTEST
void main_e1(void) {
  *L1(CTL + 0x54) = IMAGE_ID;
  *L1(CTL + 0x08) = 0xE1E1E1E1u;
  for (;;) {
    while (!*L1(CTL + 0x00)) {}
    *L1(CTL + 0x0c) = 0;
    e1_run();
    *L1(CTL + 0x0c) = 1;
    while (*L1(CTL + 0x00)) {}
  }
}
__asm__(".section .text.start,\"ax\"\n.globl _start\n_start:\n  li sp, 0xFFB02000\n  call main_e1\n1: j 1b\n");
#endif

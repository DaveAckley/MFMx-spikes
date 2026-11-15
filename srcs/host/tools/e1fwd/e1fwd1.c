/* e1fwd1.c -- Blackhole Ethernet-tile E1 image for the eth-seam car forwarder, stages 0 and 1.
 *
 * Stage 0 (modes 2,3,4): L0 data-cache poll tests.  mode 2 = poll without fence, 3 = poll with fence, 4 = first set
 *   DisLowCache (CSR 0x7c0 bit 3, as cross/src/_BUD.S does on the T6 harts) and then poll WITHOUT fence.
 * Stage 1 (modes 5,6): MFMx car forwarding between two ETH tiles over TXQ2 (TT-link L1-write packets).
 *   mode 5 FWD     : per channel, poll the OUT ring's FIFO head; when it holds a COMPLETE car (header==ankle==footer,
 *                    state INBOUND/OUTBOUND_DEPARTED) whose header word differs from the last one forwarded, send it
 *                    to the peer's IN ring slot with the same index.  Also counts arrivals on the IN ring.
 *                    (The host plays the hubs: it writes OUT slots and reads/returns IN slots over PCIe.)
 *   mode 6 REFLECT : per channel, poll the IN ring FIFO head; a new complete car is bounced straight back to the peer
 *                    in place (nonce+1, state flipped), optionally regenerating/verifying its payload.  Used to
 *                    measure link+E1 throughput with no host in the loop.
 *
 * CAR RULES (shared/include/TCMarker.h, TCBase.h): marker word = 0x2C | nonce<<8 | tcms<<16 | state<<24; packet bytes
 * = 16 (tcms 0), 32 (tcms 1), (tcms-1)*64 (tcms>=2); header = word 0, footer = last word, ankle = word (words-5)
 * when tcms>=2; a car is complete iff all of them are equal (isComplete).  Cars are owned by the far end after
 * shipping until a complete car with a changed header arrives; the FIFO order of slots is index order mod 4.
 *
 * E1 writes ONLY: its CTL/stat block, car rings (REFLECT regen), and the TXQ2 registers SRC(+0x14) SIZE(+0x18)
 * DEST(+0x1C) CMD(+0x04, value 2).  It never touches the NIU, RXQ, other queues or any other register, except the
 * CSR write in mode 4.  Everything else (queue 2 config, TX header entry, E1 reset) is done by the host.
 *
 * L0 data cache (BabyRISCV/MemoryOrdering.md): 4 x 16 B, non-coherent; this image FENCEs before polling unless
 * flag bit1 asks for the CSR route (verified by stage 0 first).  Stores to L1 followed by a TXQ CMD store need a
 * read-back dependency (stores to different regions may be processed out of order).
 */
typedef unsigned int u32;
#ifdef HOSTTEST
extern __thread char* g_l1;
extern __thread u32 g_txq[0x40];
extern void ht_txq_cmd(void);
#define L1(a) ((volatile u32*)(g_l1 + (a)))
#define TXR(off) ((volatile u32*)&g_txq[(off) / 4])
#define FENCE() __asm__ volatile("" ::: "memory")
static inline void csr_set_l0off(void) {}
static inline u32 csr_read_l0(void) { return 8u; }
static inline u32 dep0(u32 v) { u32 z = 0; __asm__ volatile("" : "+r"(z) : "r"(v)); return z; }
#else
typedef unsigned long uintptr_t_;
#define L1(a) ((volatile u32*)(uintptr_t_)(a))
#define TXR(off) ((volatile u32*)(uintptr_t_)(0xFFB92000u + (off)))
#define FENCE() __asm__ volatile("fence" ::: "memory")
static inline void csr_set_l0off(void) { u32 v = 8u; __asm__ volatile("csrrs x0, 0x7c0, %0" ::"r"(v)); }
static inline u32 csr_read_l0(void) { u32 v; __asm__ volatile("csrr %0, 0x7c0" : "=r"(v)); return v; }
static inline u32 dep0(u32 v) { u32 z; __asm__ volatile("and %0, %1, zero" : "=r"(z) : "r"(v)); return z; }
#endif

#define CTL 0x8000u
#define CHC 0x8100u              /* channel blocks: CHC + c*0x40 */
#define CAR 0x1040u              /* 4160 B: InterHubBlock */
#define RING_CARS 4u
#define CHUNK 1456u              /* payload bytes per TT-link packet: 1456+32 <= 1500, multiple of 16 */
#define MAX_TCMS 66u
enum { M_L0_NOFENCE = 2, M_L0_FENCE = 3, M_L0_CSR = 4, M_FWD = 5, M_REFLECT = 6 };
enum { F_FENCE = 1, F_CSR = 2, F_REGEN = 4 };
/* CTL words: +0 go, +4 stop, +8 ready, +C done, +10 mode, +14 slot (l0 tests), +18 flags, +1C nch, +20 verify-every, +24 ring cars (1..4, default 4),
 *            +28 csr marker (0xC5C5C5C5 before CSR write), +2C csr readback, +30 sweeps, +34..+4C l0 test results */
enum { C_OUT = 0, C_IN = 1, C_PEERIN = 2,
       S_FWD = 4, S_INSEEN = 5, S_REFL = 6, S_TXFAIL = 7, S_VCHK = 8, S_VBAD = 9, S_VWORD = 10, S_VGOT = 11, S_VEXP = 12,
       S_OUTIDX = 13, S_ININDEX = 14, S_BYTES = 15 };

static inline u32 pkt_bytes(u32 tcms) { return tcms == 0u ? 16u : (tcms == 1u ? 32u : (tcms - 1u) * 64u); }
static inline u32 mk_hdr(u32 nonce, u32 tcms, u32 state) { return 0x2cu | ((nonce & 0xffu) << 8) | (tcms << 16) | (state << 24); }
static inline u32 payload_word(u32 n, u32 w, u32 salt) { return (n * 0x01010101u) ^ (w * 0x9E3779B1u) ^ (salt * 0x00010001u); }

/* complete car at slot?  returns packet bytes (>0) and the header word, else 0 */
static inline u32 car_complete(u32 slot, u32* hdr) {
  u32 h = *L1(slot);
  if ((h & 0xffu) != 0x2cu) return 0;
  u32 st = h >> 24;
  if (st != 3u && st != 4u) return 0;
  u32 tcms = (h >> 16) & 0xffu;
  if (tcms > MAX_TCMS) return 0;
  u32 bytes = pkt_bytes(tcms);
  if (*L1(slot + bytes - 4u) != h) return 0;
  if (tcms >= 2u && *L1(slot + bytes - 20u) != h) return 0;
  *hdr = h;
  return bytes;
}

/* send [src, src+bytes) to the peer's L1 at dst via TXQ2 in <= CHUNK pieces; dtok = dependency token (see above) */
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

#define CHB(c) (CHC + (c) * 0x40u)

static inline u32 nxt(u32 i, u32 nr) { return (i + 1u >= nr) ? 0u : i + 1u; }

static void role_fwd(u32 flags, u32 nch, u32 nr) {
  u32 oidx[4] = {0, 0, 0, 0}, iidx[4] = {0, 0, 0, 0}, lasto[4][4], lasti[4][4];
  for (u32 c = 0; c < 4; ++c) for (u32 i = 0; i < 4; ++i) { lasto[c][i] = 0; lasti[c][i] = 0; }
  u32 sweeps = 0;
  for (;;) {
    if (flags & F_FENCE) FENCE();
    if (*L1(CTL + 4)) break;
    if (flags & F_FENCE) FENCE();
    for (u32 c = 0; c < nch; ++c) {
      u32 cb = CHB(c), out = *L1(cb + 4u * C_OUT), in = *L1(cb + 4u * C_IN), pin = *L1(cb + 4u * C_PEERIN);
      for (;;) {                                   /* forward every consecutive new complete OUT car */
        u32 i = oidx[c], slot = out + i * CAR, h;
        u32 bytes = car_complete(slot, &h);
        if (!bytes || h == lasto[c][i]) break;
        if (!tx_send(slot, pin + i * CAR, bytes, 0)) { *L1(cb + 4u * S_TXFAIL) = *L1(cb + 4u * S_TXFAIL) + 1u; break; }
        lasto[c][i] = h; oidx[c] = nxt(i, nr);
        *L1(cb + 4u * S_FWD) = *L1(cb + 4u * S_FWD) + 1u;
        *L1(cb + 4u * S_BYTES) = *L1(cb + 4u * S_BYTES) + bytes;
        *L1(cb + 4u * S_OUTIDX) = oidx[c];
      }
      for (;;) {                                   /* count arrivals on the IN ring (host reads them) */
        u32 i = iidx[c], slot = in + i * CAR, h;
        u32 bytes = car_complete(slot, &h);
        if (!bytes || h == lasti[c][i]) break;
        lasti[c][i] = h; iidx[c] = nxt(i, nr);
        *L1(cb + 4u * S_INSEEN) = *L1(cb + 4u * S_INSEEN) + 1u;
        *L1(cb + 4u * S_ININDEX) = iidx[c];
      }
    }
    *L1(CTL + 0x30) = ++sweeps;
  }
}

static void role_reflect(u32 flags, u32 nch, u32 vevery, u32 nr) {
  u32 iidx[4] = {0, 0, 0, 0}, lasti[4][4];
  for (u32 c = 0; c < 4; ++c) for (u32 i = 0; i < 4; ++i) lasti[c][i] = 0;
  u32 sweeps = 0, vcount = 0;
  for (;;) {
    if (flags & F_FENCE) FENCE();
    if (*L1(CTL + 4)) break;
    if (flags & F_FENCE) FENCE();
    for (u32 c = 0; c < nch; ++c) {
      u32 cb = CHB(c), in = *L1(cb + 4u * C_IN), pin = *L1(cb + 4u * C_PEERIN);
      for (;;) {
        u32 i = iidx[c], slot = in + i * CAR, h;
        u32 bytes = car_complete(slot, &h);
        if (!bytes || h == lasti[c][i]) break;
        u32 n = (h >> 8) & 0xffu, tcms = (h >> 16) & 0xffu, st = h >> 24, words = bytes / 4u;
        u32 fi = words - 1u, ai = tcms >= 2u ? words - 5u : fi, salt = (c << 2) | i;
        *L1(cb + 4u * S_INSEEN) = *L1(cb + 4u * S_INSEEN) + 1u;
        if ((flags & F_REGEN) && vevery && ++vcount >= vevery) {
          vcount = 0;
          u32 bad = 0, bw = 0, bgot = 0, bexp = 0;
          for (u32 w = 1; w < words; ++w) {
            if (w == fi || w == ai) continue;
            u32 got = *L1(slot + 4u * w), exp = payload_word(n, w, salt);
            if (got != exp) { if (!bad) { bw = w; bgot = got; bexp = exp; } bad = 1; break; }
          }
          *L1(cb + 4u * S_VCHK) = *L1(cb + 4u * S_VCHK) + 1u;
          if (bad) {
            if (*L1(cb + 4u * S_VBAD) == 0) { *L1(cb + 4u * S_VWORD) = bw; *L1(cb + 4u * S_VGOT) = bgot; *L1(cb + 4u * S_VEXP) = bexp; }
            *L1(cb + 4u * S_VBAD) = *L1(cb + 4u * S_VBAD) + 1u;
          }
        }
        u32 n2 = (n + 1u) & 0xffu, h2 = mk_hdr(n2, tcms, 7u - st);
        if (flags & F_REGEN)
          for (u32 w = 1; w < words; ++w) if (w != fi && w != ai) *L1(slot + 4u * w) = payload_word(n2, w, salt);
        if (tcms >= 2u) *L1(slot + 4u * ai) = h2;
        *L1(slot + 4u * fi) = h2;
        *L1(slot) = h2;
        u32 rb = *L1(slot);                       /* read back: all stores above are processed before the CMD store */
        if (!tx_send(slot, pin + i * CAR, bytes, rb)) { *L1(cb + 4u * S_TXFAIL) = *L1(cb + 4u * S_TXFAIL) + 1u; break; }
        lasti[c][i] = h2; iidx[c] = nxt(i, nr);
        *L1(cb + 4u * S_REFL) = *L1(cb + 4u * S_REFL) + 1u;
        *L1(cb + 4u * S_BYTES) = *L1(cb + 4u * S_BYTES) + bytes;
        *L1(cb + 4u * S_ININDEX) = iidx[c];
      }
    }
    *L1(CTL + 0x30) = ++sweeps;
  }
}

static void l0poll(u32 mode) {
  volatile u32* sl = L1(*L1(CTL + 0x14));
  u32 last = sl[0], n = 0, changes = 0, missed = 0;
  if (mode == M_L0_CSR) {
    *L1(CTL + 0x28) = 0xC5C5C5C5u;
    csr_set_l0off();
    *L1(CTL + 0x2c) = csr_read_l0() | 0x80000000u;   /* bit31 set => the CSR access completed */
    last = sl[0];
  }
  for (;;) {
    if (mode == M_L0_FENCE) FENCE();
    if (*L1(CTL + 4)) break;
    u32 v = sl[0];
    ++n;
    if (v != last) { missed += v - last - 1u; ++changes; last = v; }
  }
  *L1(CTL + 0x34) = n; *L1(CTL + 0x38) = changes; *L1(CTL + 0x3c) = missed;
}

#ifdef HOSTTEST
void e1_run(void)
#else
static void e1_run(void)
#endif
{
  u32 mode = *L1(CTL + 0x10), flags = *L1(CTL + 0x18), nch = *L1(CTL + 0x1c), ve = *L1(CTL + 0x20);
  u32 nr = *L1(CTL + 0x24);
  if (nch > 4u) nch = 4u;
  if (nr < 1u || nr > 4u) nr = 4u;
  if ((mode == M_FWD || mode == M_REFLECT) && (flags & F_CSR)) {
    *L1(CTL + 0x28) = 0xC5C5C5C5u; csr_set_l0off(); *L1(CTL + 0x2c) = csr_read_l0() | 0x80000000u;
    flags &= ~(u32)F_FENCE;
  }
  if (mode >= M_L0_NOFENCE && mode <= M_L0_CSR) l0poll(mode);
  else if (mode == M_FWD) role_fwd(flags, nch, nr);
  else if (mode == M_REFLECT) role_reflect(flags, nch, ve, nr);
}

#ifndef HOSTTEST
void main_e1(void) {
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

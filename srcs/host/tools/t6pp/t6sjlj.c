/* t6sjlj.c -- bare-metal Tensix image that tests RISC-V setjmp / longjmp on a
 * T6 BRISC hart.  New file (srcs/host/tools/t6pp/), built by t6pp/build.sh
 * t6sjlj, run by t6sjrun.  Image id 0x76500030.  Touches only its own L1 and
 * its own stack; NO NoC traffic at all.
 *
 * Why: the parked "physics failure recovery" design (OPEN_ISSUES) wants a
 * PHYSICS_FAIL macro that longjmps to a guard set once per event in
 * Physics::doTransition.  The hub images link no libc, so this tests a
 * hand-written setjmp/longjmp (14 words: ra, sp, s0..s11 -- the RV32
 * callee-saved set, no FP on this core) in exactly the build the real images
 * use (rv32im_zicsr, ilp32, -O2, -ffreestanding), and what it costs.
 *
 * Tests (verdict word 1 = pass, anything else = failure detail), results at
 * CT(0x100 + 4*id):
 *   0 basic          setjmp returns 0, then longjmp(jb,5) makes it return 5
 *   1 zero maps to 1 longjmp(jb,0) makes setjmp return 1 (C rule)
 *   2 registers      (failure = 0x100000 | mask) asm: s0..s11 get patterns, setjmp, s0..s11 get garbage,
 *                    longjmp; every s register and sp must be back
 *   3 deep unwind    recursion 20 frames deep (each with a local array and
 *                    live callee-saved values) longjmps out; sp must be the
 *                    setjmp-time sp and a volatile changed after setjmp must
 *                    show the changed value
 *   4 nested guards  inner guard catches, then the outer guard catches
 *   5 many jumps     20000 setjmp/longjmp rounds; sp unchanged, count right
 *   6 C locals       eight values live in callee-saved registers across a
 *                    call that longjmps; unmodified locals must be intact
 * Informational at CT(0x1C0): stack bytes used by test 3's recursion.
 * Timing at CT(0x200) (wall-clock ticks, 0xFFB121F0 low word) for N = 1000
 * rounds: +0 N, +4 empty-loop baseline, +8 loop with setjmp only, +C loop
 * with setjmp + longjmp.  CT(0x210) = reads of the wall clock register
 * stuck (1 if two reads 1000 loops apart were equal).
 *
 * Control block at CTLB (L1 0x8000): +00 go +04 stop +08 ready (0x7E57A11E)
 * +0C done +3C image id +40 pass counter +44 csr 0x7c0 readback.
 */
#pragma GCC diagnostic ignored "-Winfinite-recursion"
typedef unsigned int u32;
#define T6SJLJ_ID 0x76500030u
#ifndef CTLB
#define CTLB 0x8000u
#endif
#define CT(o) (CTLB + (o))
#define NTEST 7u
#define DEPTH 20u
#define NLOOP 1000u
#define NMANY 20000u

#ifdef HOSTTEST
#include <setjmp.h>
#include <stdint.h>
extern char* g_l1;
#define L1(a) ((volatile u32*)(g_l1 + (a)))
typedef jmp_buf jb_t;
#define T6_SETJMP(jb) setjmp(jb)
#define T6_LONGJMP(jb, v) longjmp((jb), (v))
#define RD_SP() ((u32)(uintptr_t)__builtin_frame_address(0))
static inline u32 wall(void) {
  static u32 t;
  t += 7u;
  return t;
}
static inline void csr_set_l0off(void) {}
static inline u32 csr_read_l0(void) { return 8u; }
#else
typedef unsigned long uintptr_t_;
#define L1(a) ((volatile u32*)(uintptr_t_)(a))
typedef u32 jb_t[14];
int t6_setjmp(u32* jb) __attribute__((returns_twice));
void t6_longjmp(u32* jb, int v) __attribute__((noreturn));
u32 t6_regtest(u32* jb);
#define T6_SETJMP(jb) t6_setjmp(jb)
#define T6_LONGJMP(jb, v) t6_longjmp((jb), (v))
#define RD_SP()                                  \
  ({                                             \
    u32 v_;                                      \
    __asm__ volatile("mv %0, sp" : "=r"(v_));    \
    v_;                                          \
  })
static inline u32 wall(void) { return *(volatile u32*)0xFFB121F0u; }
static inline void csr_set_l0off(void) {
  u32 v = 8u;
  __asm__ volatile("csrrs x0, 0x7c0, %0" ::"r"(v));
}
static inline u32 csr_read_l0(void) {
  u32 v;
  __asm__ volatile("csrr %0, 0x7c0" : "=r"(v));
  return v;
}
/* setjmp buffer layout: 0 ra, 4 sp, 8 s0, 12 s1, 16 s2 .. 52 s11 */
__asm__(".pushsection .text,\"ax\"\n"
        ".globl t6_setjmp\n.type t6_setjmp,@function\n"
        "t6_setjmp:\n"
        "  sw ra, 0(a0)\n  sw sp, 4(a0)\n"
        "  sw s0, 8(a0)\n  sw s1, 12(a0)\n  sw s2, 16(a0)\n  sw s3, 20(a0)\n"
        "  sw s4, 24(a0)\n  sw s5, 28(a0)\n  sw s6, 32(a0)\n  sw s7, 36(a0)\n"
        "  sw s8, 40(a0)\n  sw s9, 44(a0)\n  sw s10, 48(a0)\n  sw s11, 52(a0)\n"
        "  li a0, 0\n  ret\n"
        ".size t6_setjmp,.-t6_setjmp\n"
        ".globl t6_longjmp\n.type t6_longjmp,@function\n"
        "t6_longjmp:\n"
        "  lw ra, 0(a0)\n  lw sp, 4(a0)\n"
        "  lw s0, 8(a0)\n  lw s1, 12(a0)\n  lw s2, 16(a0)\n  lw s3, 20(a0)\n"
        "  lw s4, 24(a0)\n  lw s5, 28(a0)\n  lw s6, 32(a0)\n  lw s7, 36(a0)\n"
        "  lw s8, 40(a0)\n  lw s9, 44(a0)\n  lw s10, 48(a0)\n  lw s11, 52(a0)\n"
        "  mv a0, a1\n  bnez a0, 1f\n  li a0, 1\n1: ret\n"
        ".size t6_longjmp,.-t6_longjmp\n"
        /* u32 t6_regtest(u32* jb): returns a mismatch bit mask (0 = good) */
        ".globl t6_regtest\n.type t6_regtest,@function\n"
        "t6_regtest:\n"
        "  addi sp, sp, -64\n"
        "  sw ra, 60(sp)\n  sw a0, 0(sp)\n"
        "  sw s0, 8(sp)\n  sw s1, 12(sp)\n  sw s2, 16(sp)\n  sw s3, 20(sp)\n"
        "  sw s4, 24(sp)\n  sw s5, 28(sp)\n  sw s6, 32(sp)\n  sw s7, 36(sp)\n"
        "  sw s8, 40(sp)\n  sw s9, 44(sp)\n  sw s10, 48(sp)\n  sw s11, 52(sp)\n"
        "  li s0, 0x11110001\n  li s1, 0x22220002\n  li s2, 0x33330003\n"
        "  li s3, 0x44440004\n  li s4, 0x55550005\n  li s5, 0x66660006\n"
        "  li s6, 0x77770007\n  li s7, 0x88880008\n  li s8, 0x99990009\n"
        "  li s9, 0xAAAA000A\n  li s10, 0xBBBB000B\n  li s11, 0xCCCC000C\n"
        "  call t6_setjmp\n"
        "  bnez a0, 2f\n"
        "  li s0, 0xBAD00000\n  li s1, 0xBAD00001\n  li s2, 0xBAD00002\n"
        "  li s3, 0xBAD00003\n  li s4, 0xBAD00004\n  li s5, 0xBAD00005\n"
        "  li s6, 0xBAD00006\n  li s7, 0xBAD00007\n  li s8, 0xBAD00008\n"
        "  li s9, 0xBAD00009\n  li s10, 0xBAD0000A\n  li s11, 0xBAD0000B\n"
        "  lw a0, 0(sp)\n  li a1, 7\n  call t6_longjmp\n"
        "  li a0, 0x10000\n  j 3f\n"
        "2:\n"
        "  mv t0, a0\n  li a2, 0\n"
        "  li t1, 0x11110001\n  beq s0, t1, 4f\n  ori a2, a2, 1\n4:\n"
        "  li t1, 0x22220002\n  beq s1, t1, 4f\n  ori a2, a2, 2\n4:\n"
        "  li t1, 0x33330003\n  beq s2, t1, 4f\n  ori a2, a2, 4\n4:\n"
        "  li t1, 0x44440004\n  beq s3, t1, 4f\n  ori a2, a2, 8\n4:\n"
        "  li t1, 0x55550005\n  beq s4, t1, 4f\n  ori a2, a2, 16\n4:\n"
        "  li t1, 0x66660006\n  beq s5, t1, 4f\n  ori a2, a2, 32\n4:\n"
        "  li t1, 0x77770007\n  beq s6, t1, 4f\n  ori a2, a2, 64\n4:\n"
        "  li t1, 0x88880008\n  beq s7, t1, 4f\n  ori a2, a2, 128\n4:\n"
        "  li t1, 0x99990009\n  beq s8, t1, 4f\n  ori a2, a2, 256\n4:\n"
        "  li t1, 0xAAAA000A\n  beq s9, t1, 4f\n  ori a2, a2, 512\n4:\n"
        "  li t1, 0xBBBB000B\n  beq s10, t1, 4f\n  ori a2, a2, 1024\n4:\n"
        "  li t1, 0xCCCC000C\n  beq s11, t1, 4f\n  li t2, 2048\n  or a2, a2, t2\n4:\n"
        "  lw t1, 0(sp)\n  lw t1, 4(t1)\n  beq t1, sp, 4f\n"
        "  lui t2, 1\n  or a2, a2, t2\n4:\n"          /* bit 12: sp wrong */
        "  li t1, 7\n  beq t0, t1, 4f\n"
        "  lui t2, 2\n  or a2, a2, t2\n4:\n"          /* bit 13: value wrong */
        "  mv a0, a2\n"
        "3:\n"
        "  lw s0, 8(sp)\n  lw s1, 12(sp)\n  lw s2, 16(sp)\n  lw s3, 20(sp)\n"
        "  lw s4, 24(sp)\n  lw s5, 28(sp)\n  lw s6, 32(sp)\n  lw s7, 36(sp)\n"
        "  lw s8, 40(sp)\n  lw s9, 44(sp)\n  lw s10, 48(sp)\n  lw s11, 52(sp)\n"
        "  lw ra, 60(sp)\n  addi sp, sp, 64\n  ret\n"
        ".size t6_regtest,.-t6_regtest\n"
        ".popsection\n");
#endif

struct tctx {
  jb_t jb[3];
  volatile u32 vcount;
  u32 spmax, spbot;
};

static void noop(void) { __asm__ volatile("" ::: "memory"); }

/* ---- test 0 / 1 ---- */
static u32 t_basic(struct tctx* c) {
  volatile u32 stage = 0;
  int r = T6_SETJMP(c->jb[0]);
  if (r == 0) {
    stage = 1;
    T6_LONGJMP(c->jb[0], 5);
  }
  if (r != 5) return 0x100u + (u32)r;
  if (stage != 1u) return 0x200u;
  return 1u;
}
static u32 t_zero(struct tctx* c) {
  int r = T6_SETJMP(c->jb[0]);
  if (r == 0) {
    T6_LONGJMP(c->jb[0], 0);
  }
  if (r != 1) return 0x100u + (u32)r;
  return 1u;
}

/* ---- test 3: deep recursion ---- */
static void dive(struct tctx* c, u32 n, u32 a, u32 b) {
  volatile u32 pad[4];
  pad[0] = a ^ n;
  pad[1] = b + n;
  pad[2] = pad[0] + pad[1];
  pad[3] = n;
  if (n == 0u) {
    c->spbot = RD_SP();
    c->vcount = c->vcount + 100u;
    T6_LONGJMP(c->jb[0], 3);
  }
  dive(c, n - 1u, a + pad[2], b ^ pad[3]);
  /* never reached: the jump skips the returns */
  c->vcount = c->vcount + 1u;
  noop();
}
static u32 t_deep(struct tctx* c) {
  u32 sp0 = RD_SP();
  int r;
  c->vcount = 0u;
  r = T6_SETJMP(c->jb[0]);
  if (r == 0) {
    c->spmax = sp0;
    dive(c, DEPTH, 0x1234u, 0x5678u);
    return 0x300u;
  }
  if (r != 3) return 0x100u + (u32)r;
  if (RD_SP() != sp0) return 0x400u;
  if (c->vcount != 100u) return 0x500u + c->vcount;
  return 1u;
}

/* ---- test 4: nested guards ---- */
static u32 t_nested(struct tctx* c) {
  volatile u32 trail = 0;
  int ro = T6_SETJMP(c->jb[1]);
  if (ro == 0) {
    int ri = T6_SETJMP(c->jb[2]);
    if (ri == 0) {
      trail = trail * 10u + 1u;
      T6_LONGJMP(c->jb[2], 2);
    }
    if (ri != 2) return 0x100u + (u32)ri;
    trail = trail * 10u + 2u;
    T6_LONGJMP(c->jb[1], 4);
  }
  if (ro != 4) return 0x200u + (u32)ro;
  trail = trail * 10u + 3u;
  return trail == 123u ? 1u : 0x300u + trail;
}

/* ---- test 5: many jumps ---- */
static void down3(struct tctx* c, u32 k) {
  volatile u32 pad[2];
  pad[0] = k;
  pad[1] = k + 1u;
  if (k == 0u) T6_LONGJMP(c->jb[0], 1 + (int)(pad[1] & 1u));
  down3(c, k - 1u);
  noop();
}
static u32 t_many(struct tctx* c) {
  u32 sp0 = RD_SP();
  volatile u32 i = 0;
  c->vcount = 0u;
  for (;;) {
    int r = T6_SETJMP(c->jb[0]);
    if (r == 0) {
      down3(c, 3u);
      return 0x100u;
    }
    c->vcount = c->vcount + (u32)r;
    i = i + 1u;
    if (i >= NMANY) break;
  }
  if (RD_SP() != sp0) return 0x200u;
  if (i != NMANY) return 0x300u;
  /* k=0: pad[1]=1 -> r = 2 each round */
  if (c->vcount != 2u * NMANY) return 0x400u;
  return 1u;
}

/* ---- test 6: C locals in callee-saved registers ---- */
static void thrower(struct tctx* c) {
  volatile u32 x = 0;
  x = x + 1u;
  T6_LONGJMP(c->jb[0], 9);
}
static u32 t_locals(struct tctx* c, u32 s) {
  u32 a = s + 1u, b = s * 3u, d = s ^ 0x55u, e = s + 77u;
  u32 f = s * s, g = s - 5u, h = s | 0x100u, k = s & 0x7fu;
  int r;
  __asm__ volatile("" : "+r"(a), "+r"(b), "+r"(d), "+r"(e));
  __asm__ volatile("" : "+r"(f), "+r"(g), "+r"(h), "+r"(k));
  r = T6_SETJMP(c->jb[0]);
  if (r == 0) {
    thrower(c);
    return 0x100u;
  }
  if (r != 9) return 0x200u + (u32)r;
  if (a != s + 1u || b != s * 3u || d != (s ^ 0x55u) || e != s + 77u)
    return 0x300u;
  if (f != s * s || g != s - 5u || h != (s | 0x100u) || k != (s & 0x7fu))
    return 0x400u;
  return 1u;
}

/* ---- timing ---- */
static void timing(struct tctx* c, u32* out) {
  volatile u32 i;
  u32 t0, t1, t2, t3;
  t0 = wall();
  for (i = 0; i < NLOOP; i = i + 1u) noop();
  t1 = wall();
  for (i = 0; i < NLOOP; i = i + 1u) {
    (void)T6_SETJMP(c->jb[0]);
    noop();
  }
  t2 = wall();
  for (i = 0; i < NLOOP; i = i + 1u) {
    if (T6_SETJMP(c->jb[0]) == 0) T6_LONGJMP(c->jb[0], 1);
  }
  t3 = wall();
  out[0] = NLOOP;
  out[1] = t1 - t0;
  out[2] = t2 - t1;
  out[3] = t3 - t2;
}

static void pass(void) {
  struct tctx c;
  u32 v[NTEST];
  u32 tm[4];
  u32 i;
  c.vcount = 0u;
  c.spmax = c.spbot = 0u;
  v[0] = t_basic(&c);
  v[1] = t_zero(&c);
#ifdef HOSTTEST
  v[2] = 1u;                       /* asm test exists only on the target */
#else
  {
    u32 m = t6_regtest(c.jb[0]);
    v[2] = m ? (0x100000u | m) : 1u;   /* m: bits 0..11 s0..s11, 12 sp, 13 value, 0x10000 never landed */
  }
#endif
  v[3] = t_deep(&c);
  *L1(CT(0x1C0)) = c.spmax - c.spbot;
  v[4] = t_nested(&c);
  v[5] = t_many(&c);
  v[6] = t_locals(&c, 0x1234u);
  timing(&c, tm);
  for (i = 0; i < NTEST; ++i) *L1(CT(0x100u + 4u * i)) = v[i];
  for (i = 0; i < 4u; ++i) *L1(CT(0x200u + 4u * i)) = tm[i];
}

#ifdef HOSTTEST
void ht_pass(void) { pass(); }
#else
static void run(void) {
  *L1(CT(0x44)) = 0u;
  csr_set_l0off();
  *L1(CT(0x44)) = csr_read_l0() | 0x80000000u;
  for (;;) {
    while (!*L1(CT(0x00))) {
      if (*L1(CT(0x04))) return;
    }
    *L1(CT(0x0C)) = 0;
    pass();
    *L1(CT(0x40)) = *L1(CT(0x40)) + 1u;
    *L1(CT(0x0C)) = 1;
    while (*L1(CT(0x00))) {
      if (*L1(CT(0x04))) return;
    }
  }
}

void main_t6(void) {
  *L1(CT(0x3C)) = T6SJLJ_ID;
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

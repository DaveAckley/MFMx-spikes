/* hosttest_arc3.c -- native harness for t6arc3.c (T6 reads the ARC telemetry).
 * The image runs on the host with L1 as an array, the NIU registers as an
 * array, and a fake NoC that answers read commands from a fake ARC memory
 * (scratch registers, a 65-entry telemetry table laid out like the real one
 * in arcpeek10.out).  Checks that the image: walks the table, finds the
 * tags, uses node (8,0) and the modulo-16 return alignment, reads at most 12
 * words at a time, reports bad pointers (status 1), and times out (status 2)
 * when the NIU never completes.
 * Build/run: gcc -O2 -Wall -Wextra hosttest_arc3.c -o /tmp/t6arc3_ht && /tmp/t6arc3_ht
 */
#define HOSTTEST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef unsigned int u32;
char* g_l1;
volatile u32 g_niu[0x6000];
#include "t6arc3.c"

static u32 g_tptr = 0x1001a094u, g_tdata = 0x1001a1a0u;
static u32 g_stuck;       /* 1: outstanding counter never returns to 0 */
static int g_bad, g_maxwords, g_reads;

static u32 arc_word(u32 a) {
  if (a == 0x80030400u + 4u * 2u) return 5u;
  if (a == 0x80030400u + 4u * 11u) return 0x10017a08u;
  if (a == 0x80030400u + 4u * 12u) return g_tdata;
  if (a == 0x80030400u + 4u * 13u) return g_tptr;
  if (g_tptr && a >= g_tptr && a < g_tptr + 8u + 65u * 4u) {
    u32 i = (a - g_tptr) / 4u;
    if (i == 0) return 0x100u;
    if (i == 1) return 65u;
    i -= 2u;
    u32 id = i < 56u ? i : (i == 58u ? 63u : 0u);
    return id | (i << 16);
  }
  if (a >= g_tdata && a < g_tdata + 65u * 4u) {
    u32 off = (a - g_tdata) / 4u;
    switch (off) {
      case 6: return 740u;
      case 7: return 37u;
      case 11: return 0x23899au;
      case 14: return 800u;
      case 15: return 960u;
      case 16: return 800u;
      case 17: return 800u;
      case 32: return 0x3c995u;
      case 58: return 1350u;
      default: return 0x11110000u + off;
    }
  }
  return 0xBAD00000u;
}

void ht_noc_go(u32 nb) {
  u32 b = (nb - 0xFFB20000u) / 4u;
  u32 addr = g_niu[b + 0x00 / 4], node = g_niu[b + 0x08 / 4];
  u32 ret = g_niu[b + 0x0c / 4], rnode = g_niu[b + 0x14 / 4];
  u32 ctrl = g_niu[b + 0x1c / 4], bytes = g_niu[b + 0x20 / 4];
  u32 tag = g_niu[b + 0x18 / 4];
  g_niu[b + 0x40 / 4] = 0;   /* not busy */
  if (ctrl != 0u || node != 8u || rnode != 0x88u || (addr % 16u) != (ret % 16u) ||
      bytes == 0u || bytes > 48u || (bytes & 3u) || tag != (0xeu << 10)) {
    fprintf(stderr, "BAD NOC cmd ctrl=%u node=0x%x rnode=0x%x addr=0x%x ret=0x%x n=%u tag=0x%x\n",
            ctrl, node, rnode, addr, ret, bytes, tag);
    g_bad = 1;
    return;
  }
  if ((int)(bytes / 4u) > g_maxwords) g_maxwords = bytes / 4u;
  ++g_reads;
  for (u32 w = 0; w < bytes / 4u; ++w)
    *(volatile u32*)(g_l1 + ret + 4u * w) = arc_word(addr + 4u * w);
  if (g_stuck) g_niu[(0x200u + (16u + 0xeu) * 4u) / 4u] = 1u;
}

static u32 res(u32 o) { return *(volatile u32*)(g_l1 + CTLB + o); }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); ++fails; } } while (0)

int main(void) {
  g_l1 = (char*)calloc(1, 0x10000);
  ht_pass(0, 0x88u, 8u);   /* own node 0x88 = (8,2) */
  CHECK(!g_bad, "bad NoC command");
  CHECK(res(0x100) == 0, "status %u", res(0x100));
  CHECK(res(0x124) == 800, "AICLK %u", res(0x124));
  CHECK(res(0x128) == 960, "AXICLK %u", res(0x128));
  CHECK(res(0x12C) == 800, "ARCCLK %u", res(0x12C));
  CHECK(res(0x130) == 0x3c995u, "heartbeat 0x%x", res(0x130));
  CHECK(res(0x134) == 1350, "limit %u", res(0x134));
  CHECK(res(0x138) == 0x23899au, "temp");
  CHECK(res(0x13C) == 37 && res(0x140) == 740, "power/vcore");
  CHECK(res(0x148) == 800, "L2CPU0 %u", res(0x148));
  CHECK(res(0x144) == 14, "aoff %u", res(0x144));
  CHECK(res(0x118) == 0x1001a094u && res(0x114) == 0x1001a1a0u, "ptrs");
  CHECK(res(0x11C) == 0x100 && res(0x120) == 65, "ver/count");
  CHECK(res(0x104) == (u32)g_reads && res(0x108) == 0, "read counts %u/%u", res(0x104), res(0x108));
  CHECK(g_maxwords == 12, "max words per read %d", g_maxwords);
  printf("reads %d (expect 4 + 1 + 6 + 9 = 20), max words %d\n", g_reads, g_maxwords);
  CHECK(g_reads == 20, "reads %d", g_reads);

  g_tptr = 0;   /* ARC not ready: pointer zero */
  ht_pass(0, 0x88u, 8u);
  CHECK(res(0x100) == 1, "bad pointer status %u", res(0x100));
  g_tptr = 0x1001a094u;

  g_stuck = 1;  /* NIU never completes */
  ht_pass(0, 0x88u, 8u);
  CHECK(res(0x100) == 2, "timeout status %u", res(0x100));
  CHECK(res(0x108) > 0, "failed count");
  g_stuck = 0;
  g_niu[(0x200u + (16u + 0xeu) * 4u) / 4u] = 0u;

  printf("%s\n", fails ? "t6arc hosttest FAILED" : "t6arc hosttest ok");
  return fails ? 1 : 0;
}

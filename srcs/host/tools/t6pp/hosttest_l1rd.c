/* hosttest_l1rd.c -- native harness for t6l1rd.c.  A fake NoC answers read
 * commands from a peer tile whose L1 holds val(a) at address a, landing the
 * data by one of two rules: RULE 0 = at the return address (what an L1
 * source seems to do), RULE 1 = at (ret & ~63) + (src & 63) (the ARC rule).
 * Checks the image's verdicts against the prediction for each rule.
 * Build/run: gcc -O2 -Wall -Wextra hosttest_l1rd.c -o /tmp/t6l1rd_ht && /tmp/t6l1rd_ht
 */
#define HOSTTEST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int u32;
char* g_l1;
volatile u32 g_niu[0x6000];
#include "t6l1rd.c"

static int g_rule, g_bad, g_reads;

void ht_noc_go(u32 nb) {
  u32 b = (nb - 0xFFB20000u) / 4u;
  u32 src = g_niu[b + 0x00 / 4], node = g_niu[b + 0x08 / 4];
  u32 ret = g_niu[b + 0x0c / 4], rnode = g_niu[b + 0x14 / 4];
  u32 bytes = g_niu[b + 0x20 / 4];
  g_niu[b + 0x40 / 4] = 0;
  if (node != 0x82u || rnode != 0x81u || g_niu[b + 0x1c / 4] != 0u || bytes == 0u || bytes > 48u) {
    fprintf(stderr, "BAD NOC cmd node=0x%x rnode=0x%x n=%u\n", node, rnode, bytes);
    g_bad = 1;
    return;
  }
  ++g_reads;
  u32 dst = g_rule == 0 ? ret : (ret & ~63u) + (src & 63u);
  for (u32 w = 0; w < bytes / 4u; ++w)
    *(volatile u32*)(g_l1 + dst + 4u * w) = val(src + 4u * w);
}

static u32 res(u32 o) { return *(volatile u32*)(g_l1 + CTLB + o); }
static int fails;
#define CHECK(c, ...) do { if (!(c)) { printf("FAIL: " __VA_ARGS__); printf("\n"); ++fails; } } while (0)

static void run_rule(int rule) {
  g_rule = rule;
  g_reads = 0;
  memset(g_l1, 0, 0x10000);
  ht_pass(0, 0x81u, 0x82u);   /* own (1,2) = 0x81, peer (2,2) = 0x82 */
  CHECK(!g_bad, "bad NoC command");
  CHECK(g_reads == 240, "reads %d", g_reads);
  CHECK(res(0x4C0) == 240 && res(0x4C4) == 0, "counts %u/%u", res(0x4C0), res(0x4C4));
  static const u32 NS[3] = { 1u, 4u, 12u };
  for (u32 k = 0; k < 4u; ++k)
    for (u32 s = 0; s < 16u; ++s)
      for (u32 n = 0; n < 3u; ++n) {
        u32 src = PEERB + 4u * s, buf = RB + 16u * k + (src & 15u);
        u32 want = rule == 0 ? 1u : (((buf & 63u) == (src & 63u)) ? 1u : 0u);
        u32 v = res(0x100u + 4u * ((k * 16u + s) * 3u + n));
        CHECK((v & 0xffu) == want, "rule %d A k=%u S=%u n=%u verdict 0x%x want %u", rule, k, s, NS[n], v, want);
        if (rule == 1 && !want) {   /* shifted: found at the predicted word */
          u32 landed = ((buf & ~63u) - RB) / 4u + (src & 63u) / 4u;  /* word index in region */
          CHECK((v >> 8) == landed + 1u, "rule 1 A k=%u S=%u shift index %u want %u", k, s, v >> 8, landed + 1u);
        }
      }
  for (u32 s = 0; s < 16u; ++s)
    for (u32 n = 0; n < 3u; ++n) {
      u32 v = res(0x400u + 4u * (s * 3u + n));
      CHECK(v == 1u, "rule %d B S=%u n=%u verdict 0x%x", rule, s, NS[n], v);
    }
}

int main(void) {
  g_l1 = (char*)calloc(1, 0x40000);
  run_rule(0);
  run_rule(1);
  printf("%s\n", fails ? "t6l1rd hosttest FAILED" : "t6l1rd hosttest ok");
  return fails ? 1 : 0;
}

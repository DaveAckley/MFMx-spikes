/* hosttest_sjlj.c -- native harness for t6sjlj.c: runs the same C tests with
 * the libc setjmp/longjmp (the hand-written RISC-V asm and the registers test
 * exist only on the target).  Checks the verdict words and timing words.
 * Build/run: gcc -O2 -Wall -Wextra hosttest_sjlj.c -o /tmp/t6sjlj_ht && /tmp/t6sjlj_ht
 */
#define HOSTTEST 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
char* g_l1;
#include "t6sjlj.c"

int main(void) {
  u32 i, bad = 0;
  g_l1 = (char*)calloc(1, 0x10000);
  ht_pass();
  for (i = 0; i < NTEST; ++i) {
    u32 v = *L1(CT(0x100u + 4u * i));
    printf("test %u verdict 0x%x\n", i, v);
    if (v != 1u) ++bad;
  }
  printf("timing N=%u base=%u sj=%u sjlj=%u, stack depth %u\n",
         *L1(CT(0x200)), *L1(CT(0x204)), *L1(CT(0x208)), *L1(CT(0x20C)),
         *L1(CT(0x1C0)));
  if (*L1(CT(0x200)) != NLOOP) ++bad;
  if (*L1(CT(0x1C0)) == 0u) ++bad;
  printf(bad ? "FAIL\n" : "t6sjlj_hosttest ok\n");
  return bad ? 1 : 0;
}

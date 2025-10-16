#ifndef DEATH_H
#define DEATH_H

#include <cstdint>
#include <cstddef>
#include <cstdlib>

#include <stdio.h>
#include <errno.h>

#define FATAL(fmt, ...) do {fprintf(stderr, "%s:%d: FATAL ERROR: " fmt "\n",__FILE__,__LINE__,##__VA_ARGS__); exit(1);} while(0)
#define ASSERT(cond) ASSERT_DBG(cond)
#define ASSERT_DBG(cond) if (cond) {} else FATAL("Assertion failed: %s", #cond)
#define ASSERT_NONDBG(cond) (cond)

#endif /* DEATH_H */

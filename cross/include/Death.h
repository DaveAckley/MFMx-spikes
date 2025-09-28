#ifndef DEATH_H
#define DEATH_H

#include <cstdint>
#include <cstddef>
#include <cstdlib>

#include <stdio.h>
#include <errno.h>

#define FATAL(fmt, ...) do { /*NOTHING JUST HANG HAHAHAHAHA */ } while(1)
#define ASSERT(cond) ASSERT_DBG(cond)
#define ASSERT_DBG(cond) if (cond) {} else FATAL("Assertion failed: %s", #cond)
#define ASSERT_NONDBG(cond) (cond)

#endif /* DEATH_H */

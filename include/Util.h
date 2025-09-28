#ifndef UTIL_H
#define UTIL_H

#include <cstdint>
#include <cstddef>
#include <cstdlib>

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include "itype.h"

#define FATAL(fmt, ...) do {fprintf(stderr, "%s:%d: FATAL ERROR: " fmt "\n",__FILE__,__LINE__,##__VA_ARGS__); exit(1);} while(0)

#endif /* UTIL_H */

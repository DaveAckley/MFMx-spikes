#pragma once
// Test shim: the logging/assert macros lzmfmx.cpp uses, as no-ops.
#define EACH(N, STMT) do { } while (0)
#define SNAP(N, STMT) do { } while (0)
#define HBMARK do { } while (0)
#define LOGMARK do { } while (0)
#define LOGPTAG(T, V) do { } while (0)
#define HBPTAG(T, V) do { } while (0)
#define LOGPX(X) do { } while (0)
#define HBPX(X) do { } while (0)
inline const char * formatCountedByte(unsigned, int) { return ""; }

#pragma once
#include <cstring>
inline void * memset_s(void * p, int c, unsigned long n) { return std::memset(p, c, n); }

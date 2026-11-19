#pragma once
#include <cstdio>
#include <cstdlib>
#define FAIL(X) do { std::fprintf(stderr, "FAIL(%s)\n", #X); std::abort(); } while (0)
#define MFM_API_ASSERT(C, X) do { if (!(C)) FAIL(X); } while (0)
#define MFM_API_ASSERT_NONNULL(P) MFM_API_ASSERT((P) != nullptr, NULLPTR)

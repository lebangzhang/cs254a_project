#pragma once

#include <stdint.h>

#ifndef NUM_THREADS
#define NUM_THREADS 8
#endif

enum : uint32_t {
  kCaseUniqueDst     = 1u << 0,
  kCaseDstEqSrcA     = 1u << 1,
  kCaseDstEqSrcB     = 1u << 2,
  kCasePartialOverlap = 1u << 3,
  kCaseDstEqSrcATwice = 1u << 4,
  kCaseDstEqSrcBTwice = 1u << 5,
  kCaseCount         = 6,
};

struct kernel_arg_t {
  uint64_t A_addr;
  uint64_t B0_addr;
  uint64_t B1_addr;
  uint64_t C_addr;
  uint32_t case_mask;
  uint32_t reserved;
};

#pragma once

#include <stdint.h>

#ifdef NUM_THREADS
#if NUM_THREADS != 8
#error "wmma_vv_rvv regression is fixed to NUM_THREADS=8"
#endif
#else
#define NUM_THREADS 8
#endif

struct kernel_arg_t {
  uint64_t A_addr;
  uint64_t B_addr;
  uint64_t C_addr;
};

constexpr uint32_t kProblemM = 64;
constexpr uint32_t kProblemN = 64;
constexpr uint32_t kProblemK = 64;

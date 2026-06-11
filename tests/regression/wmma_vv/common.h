#pragma once

#include <stdint.h>

static constexpr uint32_t kMatrixSize = 64;

#ifdef NUM_THREADS
#if NUM_THREADS != 8
#error "wmma_vv regression is fixed to NUM_THREADS=8"
#endif
#else
#define NUM_THREADS 8
#endif

struct kernel_arg_t {
  uint64_t A_addr;
  uint64_t B_addr;
  uint64_t C_addr;
};

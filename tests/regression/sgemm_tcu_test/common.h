#pragma once

#include <stdint.h>

#ifdef NUM_THREADS
#if NUM_THREADS != 8
#error "sgemm_tcu_baseline regression is fixed to NUM_THREADS=8"
#endif
#else
#define NUM_THREADS 8
#endif

#ifndef ITYPE
#define ITYPE tf32
#endif

#ifndef OTYPE
#define OTYPE fp32
#endif

struct kernel_arg_t {
  uint64_t A_addr;
  uint64_t B_addr;
  uint64_t C_addr;
};

#pragma once

#include <stdint.h>

#ifndef NUM_THREADS
#define NUM_THREADS 8
#endif

struct kernel_arg_t {
  uint64_t A_addr;
  uint64_t B_addr;
  uint64_t C_addr;
};

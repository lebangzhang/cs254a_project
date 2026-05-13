#pragma once

#include <stdint.h>

struct kernel_arg_t {
  uint64_t A_addr;
  uint64_t B_addr;
  uint64_t C_addr;
};

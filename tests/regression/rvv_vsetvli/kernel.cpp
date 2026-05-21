#include <stdint.h>
#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  if (blockIdx.x != 0 || threadIdx.x != 0)
    return;

  uint32_t actual_vl;
  auto out = reinterpret_cast<uint32_t* __restrict>(arg->out_addr);

  __asm__ __volatile__(
      "vsetvli %[actual], %[avl], e32, m1, ta, ma"
      : [actual] "=r"(actual_vl)
      : [avl] "r"(arg->avl));

  out[0] = actual_vl;
  out[1] = arg->expected_vl;
}

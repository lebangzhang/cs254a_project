#include <stdint.h>
#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  if (blockIdx.x != 0 || threadIdx.x != 0)
    return;

  uint32_t actual_vsetvli;
  uint32_t actual_vsetivli;
  uint32_t actual_vsetvl;
  auto out = reinterpret_cast<uint32_t* __restrict>(arg->out_addr);

  __asm__ __volatile__(
      "vsetvli %[actual], %[avl], e32, m1, ta, ma"
      : [actual] "=r"(actual_vsetvli)
      : [avl] "r"(arg->avl));

  __asm__ __volatile__(
      "vsetivli %[actual], 5, e32, m1, ta, ma"
      : [actual] "=r"(actual_vsetivli));

  __asm__ __volatile__(
      "vsetvl %[actual], %[avl], %[vtype]"
      : [actual] "=r"(actual_vsetvl)
      : [avl] "r"(arg->avl), [vtype] "r"(arg->vtype));

  out[0] = actual_vsetvli;
  out[1] = arg->expected_vsetvli;
  out[2] = actual_vsetivli;
  out[3] = arg->expected_vsetivli;
  out[4] = actual_vsetvl;
  out[5] = arg->expected_vsetvl;
}

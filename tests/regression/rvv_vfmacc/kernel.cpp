#include <stdint.h>
#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  auto acc = reinterpret_cast<float* __restrict>(arg->acc_addr);
  auto lhs = reinterpret_cast<float* __restrict>(arg->lhs_addr);
  auto rhs = reinterpret_cast<float* __restrict>(arg->rhs_addr);
  auto dst = reinterpret_cast<float* __restrict>(arg->dst_addr);

  int tile = blockIdx.x * blockDim.x + threadIdx.x;
  int start = tile * (int)arg->vlen;
  if (start >= (int)arg->size)
    return;

  uint32_t remaining = arg->size - start;
  uint32_t vl = (remaining < arg->vlen) ? remaining : arg->vlen;

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m1, ta, ma\n\t"
      "vle32.v v8, (%[acc])\n\t"
      "vle32.v v10, (%[lhs])\n\t"
      "vle32.v v12, (%[rhs])\n\t"
      "vfmacc.vv v8, v10, v12\n\t"
      "vse32.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl),
        [acc] "r"(acc + start),
        [lhs] "r"(lhs + start),
        [rhs] "r"(rhs + start),
        [dst] "r"(dst + start)
      : "v8", "v10", "v12", "memory");
}

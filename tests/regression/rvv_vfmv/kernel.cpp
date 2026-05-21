#include <stdint.h>
#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  auto scalar = reinterpret_cast<float* __restrict>(arg->scalar_addr);
  auto dst = reinterpret_cast<float* __restrict>(arg->dst_addr);

  int tile = blockIdx.x * blockDim.x + threadIdx.x;
  int start = tile * (int)arg->vlen;
  if (start >= (int)arg->size)
    return;

  uint32_t remaining = arg->size - start;
  uint32_t vl = (remaining < arg->vlen) ? remaining : arg->vlen;
  float value = scalar[0];

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m1, ta, ma\n\t"
      "vfmv.v.f v8, %[value]\n\t"
      "vse32.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl), [value] "f"(value), [dst] "r"(dst + start)
      : "v8", "memory");
}

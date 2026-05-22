#include <stdint.h>
#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  auto scalar = reinterpret_cast<float* __restrict>(arg->scalar_addr);
  const uint32_t tid = threadIdx.x;

  if (blockIdx.x != 0 || tid >= 4)
    return;

  const uint32_t thread_span = 48;
  auto src = reinterpret_cast<float* __restrict>(arg->src_addr) + tid * thread_span;
  auto dst = reinterpret_cast<float* __restrict>(arg->dst_addr) + tid * thread_span;

  float value = scalar[tid];
  const uint32_t vl1 = 1;
  const uint32_t vl4 = 4;
  const uint32_t vl8 = 8;
  const uint32_t vl16 = 16;

  float out0;
  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m1, ta, ma\n\t"
      "vle32.v v8, (%[src])\n\t"
      "vfmv.f.s %[out], v8\n\t"
      : [out] "=f"(out0)
      : [vl] "r"(vl1), [src] "r"(src + 0)
      : "v8", "memory");
  dst[0] = out0;

  float out1;
  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m1, ta, ma\n\t"
      "vle32.v v8, (%[src])\n\t"
      "vfmv.f.s %[out], v8\n\t"
      : [out] "=f"(out1)
      : [vl] "r"(vl8), [src] "r"(src + 8)
      : "v8", "memory");
  dst[1] = out1;

  float out2;
  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m2, ta, ma\n\t"
      "vle32.v v8, (%[src])\n\t"
      "vfmv.f.s %[out], v8\n\t"
      : [out] "=f"(out2)
      : [vl] "r"(vl16), [src] "r"(src + 16)
      : "v8", "v9", "memory");
  dst[2] = out2;

  float out3;
  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m4, ta, ma\n\t"
      "vle32.v v8, (%[src])\n\t"
      "vfmv.f.s %[out], v8\n\t"
      : [out] "=f"(out3)
      : [vl] "r"(vl16), [src] "r"(src + 32)
      : "v8", "v9", "v10", "v11", "memory");
  dst[3] = out3;

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m1, ta, ma\n\t"
      "vfmv.v.f v8, %[value]\n\t"
      "vse32.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl4), [value] "f"(value), [dst] "r"(dst + 8)
      : "v8", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m2, ta, ma\n\t"
      "vfmv.v.f v8, %[value]\n\t"
      "vse32.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl8), [value] "f"(value), [dst] "r"(dst + 16)
      : "v8", "v9", "memory");
}

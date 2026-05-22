#include <stdint.h>
#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  if (blockIdx.x != 0 || threadIdx.x != 0)
    return;

  auto src = reinterpret_cast<uint8_t* __restrict>(arg->src_addr);
  auto dst = reinterpret_cast<uint8_t* __restrict>(arg->dst_addr);
  auto idx = reinterpret_cast<uint32_t* __restrict>(arg->idx_addr);

  const uint32_t vl8 = 16;
  const uint32_t vl16 = 16;
  const uint32_t vl32 = 8;
  const uint32_t vl64 = 4;
  const uint32_t vl32_m2 = 16;
  const uint32_t stride32 = 8;

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e8, m1, ta, ma\n\t"
      "vle8.v v8, (%[src])\n\t"
      "vse8.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl8), [src] "r"(src + 0), [dst] "r"(dst + 0)
      : "v8", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e16, m1, ta, ma\n\t"
      "vle16.v v8, (%[src])\n\t"
      "vse16.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl16), [src] "r"(src + 64), [dst] "r"(dst + 64)
      : "v8", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m1, ta, ma\n\t"
      "vle32.v v8, (%[src])\n\t"
      "vse32.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl32), [src] "r"(src + 128), [dst] "r"(dst + 128)
      : "v8", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e64, m1, ta, ma\n\t"
      "vle64.v v8, (%[src])\n\t"
      "vse64.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl64), [src] "r"(src + 192), [dst] "r"(dst + 192)
      : "v8", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m2, ta, ma\n\t"
      "vle32.v v8, (%[src])\n\t"
      "vse32.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl32_m2), [src] "r"(src + 256), [dst] "r"(dst + 256)
      : "v8", "v9", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m1, ta, ma\n\t"
      "vlse32.v v8, (%[src]), %[stride]\n\t"
      "vse32.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl32), [src] "r"(src + 384), [dst] "r"(dst + 384),
        [stride] "r"(stride32)
      : "v8", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m1, ta, ma\n\t"
      "vle32.v v4, (%[idx])\n\t"
      "vluxei32.v v8, (%[src]), v4\n\t"
      "vse32.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl32), [idx] "r"(idx), [src] "r"(src + 512),
        [dst] "r"(dst + 512)
      : "v4", "v8", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m1, ta, ma\n\t"
      "vlseg2e32.v v8, (%[src])\n\t"
      "vsseg2e32.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl32), [src] "r"(src + 640), [dst] "r"(dst + 640)
      : "v8", "v9", "memory");
}

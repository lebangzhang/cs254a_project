#include <stdint.h>
#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  auto f32_acc = reinterpret_cast<float* __restrict>(arg->f32_acc_addr);
  auto f32_lhs = reinterpret_cast<float* __restrict>(arg->f32_lhs_addr);
  auto f32_rhs = reinterpret_cast<float* __restrict>(arg->f32_rhs_addr);
  auto f32_dst = reinterpret_cast<float* __restrict>(arg->f32_dst_addr);
  auto f64_acc = reinterpret_cast<double* __restrict>(arg->f64_acc_addr);
  auto f64_lhs = reinterpret_cast<double* __restrict>(arg->f64_lhs_addr);
  auto f64_rhs = reinterpret_cast<double* __restrict>(arg->f64_rhs_addr);
  auto f64_dst = reinterpret_cast<double* __restrict>(arg->f64_dst_addr);

  if (blockIdx.x != 0 || threadIdx.x != 0)
    return;

  const uint32_t vl2 = 2;
  const uint32_t vl4 = 4;
  const uint32_t vl8 = 8;
  const uint32_t vl16 = 16;
  float f32_scalar = f32_lhs[120];
  double f64_scalar = f64_lhs[60];

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m1, ta, ma\n\t"
      "vle32.v v8, (%[acc])\n\t"
      "vle32.v v10, (%[lhs])\n\t"
      "vle32.v v12, (%[rhs])\n\t"
      "vfmacc.vv v8, v10, v12\n\t"
      "vse32.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl4), [acc] "r"(f32_acc + 0), [lhs] "r"(f32_lhs + 0),
        [rhs] "r"(f32_rhs + 0), [dst] "r"(f32_dst + 0)
      : "v8", "v10", "v12", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m2, ta, ma\n\t"
      "vle32.v v8, (%[acc])\n\t"
      "vle32.v v10, (%[lhs])\n\t"
      "vle32.v v12, (%[rhs])\n\t"
      "vfmacc.vv v8, v10, v12\n\t"
      "vse32.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl8), [acc] "r"(f32_acc + 16), [lhs] "r"(f32_lhs + 16),
        [rhs] "r"(f32_rhs + 16), [dst] "r"(f32_dst + 16)
      : "v8", "v9", "v10", "v11", "v12", "v13", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m4, ta, ma\n\t"
      "vle32.v v8, (%[acc])\n\t"
      "vle32.v v12, (%[lhs])\n\t"
      "vle32.v v16, (%[rhs])\n\t"
      "vfmacc.vv v8, v12, v16\n\t"
      "vse32.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl16), [acc] "r"(f32_acc + 32), [lhs] "r"(f32_lhs + 32),
        [rhs] "r"(f32_rhs + 32), [dst] "r"(f32_dst + 32)
      : "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
        "v16", "v17", "v18", "v19", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m1, ta, ma\n\t"
      "vle32.v v8, (%[acc])\n\t"
      "vle32.v v10, (%[rhs])\n\t"
      "vfmacc.vf v8, %[scalar], v10\n\t"
      "vse32.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl4), [acc] "r"(f32_acc + 64), [rhs] "r"(f32_rhs + 64),
        [scalar] "f"(f32_scalar), [dst] "r"(f32_dst + 64)
      : "v8", "v10", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m2, ta, ma\n\t"
      "vle32.v v8, (%[acc])\n\t"
      "vle32.v v10, (%[rhs])\n\t"
      "vfmacc.vf v8, %[scalar], v10\n\t"
      "vse32.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl8), [acc] "r"(f32_acc + 80), [rhs] "r"(f32_rhs + 80),
        [scalar] "f"(f32_scalar), [dst] "r"(f32_dst + 80)
      : "v8", "v9", "v10", "v11", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e32, m4, ta, ma\n\t"
      "vle32.v v8, (%[acc])\n\t"
      "vle32.v v12, (%[rhs])\n\t"
      "vfmacc.vf v8, %[scalar], v12\n\t"
      "vse32.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl16), [acc] "r"(f32_acc + 96), [rhs] "r"(f32_rhs + 96),
        [scalar] "f"(f32_scalar), [dst] "r"(f32_dst + 96)
      : "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e64, m1, ta, ma\n\t"
      "vle64.v v8, (%[acc])\n\t"
      "vle64.v v10, (%[lhs])\n\t"
      "vle64.v v12, (%[rhs])\n\t"
      "vfmacc.vv v8, v10, v12\n\t"
      "vse64.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl2), [acc] "r"(f64_acc + 0), [lhs] "r"(f64_lhs + 0),
        [rhs] "r"(f64_rhs + 0), [dst] "r"(f64_dst + 0)
      : "v8", "v10", "v12", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e64, m2, ta, ma\n\t"
      "vle64.v v8, (%[acc])\n\t"
      "vle64.v v10, (%[lhs])\n\t"
      "vle64.v v12, (%[rhs])\n\t"
      "vfmacc.vv v8, v10, v12\n\t"
      "vse64.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl4), [acc] "r"(f64_acc + 8), [lhs] "r"(f64_lhs + 8),
        [rhs] "r"(f64_rhs + 8), [dst] "r"(f64_dst + 8)
      : "v8", "v9", "v10", "v11", "v12", "v13", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e64, m4, ta, ma\n\t"
      "vle64.v v8, (%[acc])\n\t"
      "vle64.v v12, (%[lhs])\n\t"
      "vle64.v v16, (%[rhs])\n\t"
      "vfmacc.vv v8, v12, v16\n\t"
      "vse64.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl8), [acc] "r"(f64_acc + 16), [lhs] "r"(f64_lhs + 16),
        [rhs] "r"(f64_rhs + 16), [dst] "r"(f64_dst + 16)
      : "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
        "v16", "v17", "v18", "v19", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e64, m1, ta, ma\n\t"
      "vle64.v v8, (%[acc])\n\t"
      "vle64.v v10, (%[rhs])\n\t"
      "vfmacc.vf v8, %[scalar], v10\n\t"
      "vse64.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl2), [acc] "r"(f64_acc + 32), [rhs] "r"(f64_rhs + 32),
        [scalar] "f"(f64_scalar), [dst] "r"(f64_dst + 32)
      : "v8", "v10", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e64, m2, ta, ma\n\t"
      "vle64.v v8, (%[acc])\n\t"
      "vle64.v v10, (%[rhs])\n\t"
      "vfmacc.vf v8, %[scalar], v10\n\t"
      "vse64.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl4), [acc] "r"(f64_acc + 40), [rhs] "r"(f64_rhs + 40),
        [scalar] "f"(f64_scalar), [dst] "r"(f64_dst + 40)
      : "v8", "v9", "v10", "v11", "memory");

  __asm__ __volatile__(
      "vsetvli zero, %[vl], e64, m4, ta, ma\n\t"
      "vle64.v v8, (%[acc])\n\t"
      "vle64.v v12, (%[rhs])\n\t"
      "vfmacc.vf v8, %[scalar], v12\n\t"
      "vse64.v v8, (%[dst])\n\t"
      :
      : [vl] "r"(vl8), [acc] "r"(f64_acc + 48), [rhs] "r"(f64_rhs + 48),
        [scalar] "f"(f64_scalar), [dst] "r"(f64_dst + 48)
      : "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "memory");
}

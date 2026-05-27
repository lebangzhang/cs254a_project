#include "common.h"
#include <tensor_cfg.h>
#include <vx_intrinsics.h>

namespace vt = vortex::tensor;
using cfg = vt::wmma_config_t<NUM_THREADS>;

static_assert(NUM_THREADS == 4 || NUM_THREADS == 8 || NUM_THREADS == 16,
              "wmma_vv_rvv baseline currently supports NUM_THREADS=4/8/16");

namespace {

#if NUM_THREADS == 4
static inline void issue_rvv_tile(const float* A, const float* B) {
  const float zero = 0.0f;
  __asm__ volatile(
      "vfmv.v.f v12, %[zero]\n\t"
      "vfmv.v.f v13, %[zero]\n\t"
      "vfmv.v.f v14, %[zero]\n\t"
      "vfmv.v.f v15, %[zero]\n\t"
      "vfmv.v.f v16, %[zero]\n\t"
      "vfmv.v.f v17, %[zero]\n\t"
      "vfmv.v.f v18, %[zero]\n\t"
      "vfmv.v.f v19, %[zero]\n\t"
      :
      : [zero] "f"(zero)
      : "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19");

  for (uint32_t k = 0; k < cfg::tileK; ++k) {
    const float* brow = B + k * cfg::tileN;
    float a0 = A[0 * cfg::tileK + k];
    float a1 = A[1 * cfg::tileK + k];
    float a2 = A[2 * cfg::tileK + k];
    float a3 = A[3 * cfg::tileK + k];
    float a4 = A[4 * cfg::tileK + k];
    float a5 = A[5 * cfg::tileK + k];
    float a6 = A[6 * cfg::tileK + k];
    float a7 = A[7 * cfg::tileK + k];

    __asm__ volatile(
        "vle32.v v8, (%[brow])\n\t"
        "vfmacc.vf v12, %[a0], v8\n\t"
        "vfmacc.vf v13, %[a1], v8\n\t"
        "vfmacc.vf v14, %[a2], v8\n\t"
        "vfmacc.vf v15, %[a3], v8\n\t"
        "vfmacc.vf v16, %[a4], v8\n\t"
        "vfmacc.vf v17, %[a5], v8\n\t"
        "vfmacc.vf v18, %[a6], v8\n\t"
        "vfmacc.vf v19, %[a7], v8\n\t"
        :
        : [brow] "r"(brow),
          [a0] "f"(a0), [a1] "f"(a1), [a2] "f"(a2), [a3] "f"(a3),
          [a4] "f"(a4), [a5] "f"(a5), [a6] "f"(a6), [a7] "f"(a7)
        : "v8", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19", "memory");
  }
}

static inline void store_c_regs(float* C) {
  __asm__ volatile(
      "vse32.v v12, (%[c0])\n\t"
      "vse32.v v13, (%[c1])\n\t"
      "vse32.v v14, (%[c2])\n\t"
      "vse32.v v15, (%[c3])\n\t"
      "vse32.v v16, (%[c4])\n\t"
      "vse32.v v17, (%[c5])\n\t"
      "vse32.v v18, (%[c6])\n\t"
      "vse32.v v19, (%[c7])"
      :
      : [c0] "r"(C + 0 * cfg::tileN),
        [c1] "r"(C + 1 * cfg::tileN),
        [c2] "r"(C + 2 * cfg::tileN),
        [c3] "r"(C + 3 * cfg::tileN),
        [c4] "r"(C + 4 * cfg::tileN),
        [c5] "r"(C + 5 * cfg::tileN),
        [c6] "r"(C + 6 * cfg::tileN),
        [c7] "r"(C + 7 * cfg::tileN)
      : "memory");
}
#elif NUM_THREADS == 8
static inline void issue_rvv_tile(const float* A, const float* B) {
  const float zero = 0.0f;
  __asm__ volatile(
      "vfmv.v.f v16, %[zero]\n\t"
      "vfmv.v.f v17, %[zero]\n\t"
      "vfmv.v.f v18, %[zero]\n\t"
      "vfmv.v.f v19, %[zero]\n\t"
      "vfmv.v.f v20, %[zero]\n\t"
      "vfmv.v.f v21, %[zero]\n\t"
      "vfmv.v.f v22, %[zero]\n\t"
      "vfmv.v.f v23, %[zero]\n\t"
      :
      : [zero] "f"(zero)
      : "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23");

  for (uint32_t k = 0; k < cfg::tileK; ++k) {
    const float* brow = B + k * cfg::tileN;
    float a0 = A[0 * cfg::tileK + k];
    float a1 = A[1 * cfg::tileK + k];
    float a2 = A[2 * cfg::tileK + k];
    float a3 = A[3 * cfg::tileK + k];
    float a4 = A[4 * cfg::tileK + k];
    float a5 = A[5 * cfg::tileK + k];
    float a6 = A[6 * cfg::tileK + k];
    float a7 = A[7 * cfg::tileK + k];

    __asm__ volatile(
        "vle32.v v8, (%[brow])\n\t"
        "vfmacc.vf v16, %[a0], v8\n\t"
        "vfmacc.vf v17, %[a1], v8\n\t"
        "vfmacc.vf v18, %[a2], v8\n\t"
        "vfmacc.vf v19, %[a3], v8\n\t"
        "vfmacc.vf v20, %[a4], v8\n\t"
        "vfmacc.vf v21, %[a5], v8\n\t"
        "vfmacc.vf v22, %[a6], v8\n\t"
        "vfmacc.vf v23, %[a7], v8\n\t"
        :
        : [brow] "r"(brow),
          [a0] "f"(a0), [a1] "f"(a1), [a2] "f"(a2), [a3] "f"(a3),
          [a4] "f"(a4), [a5] "f"(a5), [a6] "f"(a6), [a7] "f"(a7)
        : "v8", "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23", "memory");
  }
}

static inline void store_c_regs(float* C) {
  __asm__ volatile(
      "vse32.v v16, (%[c0])\n\t"
      "vse32.v v17, (%[c1])\n\t"
      "vse32.v v18, (%[c2])\n\t"
      "vse32.v v19, (%[c3])\n\t"
      "vse32.v v20, (%[c4])\n\t"
      "vse32.v v21, (%[c5])\n\t"
      "vse32.v v22, (%[c6])\n\t"
      "vse32.v v23, (%[c7])"
      :
      : [c0] "r"(C + 0 * cfg::tileN),
        [c1] "r"(C + 1 * cfg::tileN),
        [c2] "r"(C + 2 * cfg::tileN),
        [c3] "r"(C + 3 * cfg::tileN),
        [c4] "r"(C + 4 * cfg::tileN),
        [c5] "r"(C + 5 * cfg::tileN),
        [c6] "r"(C + 6 * cfg::tileN),
        [c7] "r"(C + 7 * cfg::tileN)
      : "memory");
}
#elif NUM_THREADS == 16
static inline void issue_rvv_tile(const float* A, const float* B) {
  const float zero = 0.0f;
  __asm__ volatile(
      "vfmv.v.f v0,  %[zero]\n\t"
      "vfmv.v.f v1,  %[zero]\n\t"
      "vfmv.v.f v2,  %[zero]\n\t"
      "vfmv.v.f v3,  %[zero]\n\t"
      "vfmv.v.f v4,  %[zero]\n\t"
      "vfmv.v.f v5,  %[zero]\n\t"
      "vfmv.v.f v6,  %[zero]\n\t"
      "vfmv.v.f v7,  %[zero]\n\t"
      "vfmv.v.f v8,  %[zero]\n\t"
      "vfmv.v.f v9,  %[zero]\n\t"
      "vfmv.v.f v10, %[zero]\n\t"
      "vfmv.v.f v11, %[zero]\n\t"
      "vfmv.v.f v12, %[zero]\n\t"
      "vfmv.v.f v13, %[zero]\n\t"
      "vfmv.v.f v14, %[zero]\n\t"
      "vfmv.v.f v15, %[zero]\n\t"
      :
      : [zero] "f"(zero)
      : "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
        "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15");

  for (uint32_t k = 0; k < cfg::tileK; ++k) {
    const float* brow = B + k * cfg::tileN;
    float a0  = A[0  * cfg::tileK + k];
    float a1  = A[1  * cfg::tileK + k];
    float a2  = A[2  * cfg::tileK + k];
    float a3  = A[3  * cfg::tileK + k];
    float a4  = A[4  * cfg::tileK + k];
    float a5  = A[5  * cfg::tileK + k];
    float a6  = A[6  * cfg::tileK + k];
    float a7  = A[7  * cfg::tileK + k];
    float a8  = A[8  * cfg::tileK + k];
    float a9  = A[9  * cfg::tileK + k];
    float a10 = A[10 * cfg::tileK + k];
    float a11 = A[11 * cfg::tileK + k];
    float a12 = A[12 * cfg::tileK + k];
    float a13 = A[13 * cfg::tileK + k];
    float a14 = A[14 * cfg::tileK + k];
    float a15 = A[15 * cfg::tileK + k];

    __asm__ volatile(
        "vle32.v v16, (%[brow])\n\t"
        "vfmacc.vf v0,  %[a0],  v16\n\t"
        "vfmacc.vf v1,  %[a1],  v16\n\t"
        "vfmacc.vf v2,  %[a2],  v16\n\t"
        "vfmacc.vf v3,  %[a3],  v16\n\t"
        "vfmacc.vf v4,  %[a4],  v16\n\t"
        "vfmacc.vf v5,  %[a5],  v16\n\t"
        "vfmacc.vf v6,  %[a6],  v16\n\t"
        "vfmacc.vf v7,  %[a7],  v16\n\t"
        "vfmacc.vf v8,  %[a8],  v16\n\t"
        "vfmacc.vf v9,  %[a9],  v16\n\t"
        "vfmacc.vf v10, %[a10], v16\n\t"
        "vfmacc.vf v11, %[a11], v16\n\t"
        "vfmacc.vf v12, %[a12], v16\n\t"
        "vfmacc.vf v13, %[a13], v16\n\t"
        "vfmacc.vf v14, %[a14], v16\n\t"
        "vfmacc.vf v15, %[a15], v16\n\t"
        :
        : [brow] "r"(brow),
          [a0] "f"(a0), [a1] "f"(a1), [a2] "f"(a2), [a3] "f"(a3),
          [a4] "f"(a4), [a5] "f"(a5), [a6] "f"(a6), [a7] "f"(a7),
          [a8] "f"(a8), [a9] "f"(a9), [a10] "f"(a10), [a11] "f"(a11),
          [a12] "f"(a12), [a13] "f"(a13), [a14] "f"(a14), [a15] "f"(a15)
        : "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7",
          "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15",
          "v16", "memory");
  }
}

static inline void store_c_regs(float* C) {
  __asm__ volatile(
      "vse32.v v0,  (%[c0])\n\t"
      "vse32.v v1,  (%[c1])\n\t"
      "vse32.v v2,  (%[c2])\n\t"
      "vse32.v v3,  (%[c3])\n\t"
      "vse32.v v4,  (%[c4])\n\t"
      "vse32.v v5,  (%[c5])\n\t"
      "vse32.v v6,  (%[c6])\n\t"
      "vse32.v v7,  (%[c7])\n\t"
      "vse32.v v8,  (%[c8])\n\t"
      "vse32.v v9,  (%[c9])\n\t"
      "vse32.v v10, (%[c10])\n\t"
      "vse32.v v11, (%[c11])\n\t"
      "vse32.v v12, (%[c12])\n\t"
      "vse32.v v13, (%[c13])\n\t"
      "vse32.v v14, (%[c14])\n\t"
      "vse32.v v15, (%[c15])"
      :
      : [c0]  "r"(C + 0  * cfg::tileN),
        [c1]  "r"(C + 1  * cfg::tileN),
        [c2]  "r"(C + 2  * cfg::tileN),
        [c3]  "r"(C + 3  * cfg::tileN),
        [c4]  "r"(C + 4  * cfg::tileN),
        [c5]  "r"(C + 5  * cfg::tileN),
        [c6]  "r"(C + 6  * cfg::tileN),
        [c7]  "r"(C + 7  * cfg::tileN),
        [c8]  "r"(C + 8  * cfg::tileN),
        [c9]  "r"(C + 9  * cfg::tileN),
        [c10] "r"(C + 10 * cfg::tileN),
        [c11] "r"(C + 11 * cfg::tileN),
        [c12] "r"(C + 12 * cfg::tileN),
        [c13] "r"(C + 13 * cfg::tileN),
        [c14] "r"(C + 14 * cfg::tileN),
        [c15] "r"(C + 15 * cfg::tileN)
      : "memory");
}
#endif

} // namespace

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  if (vx_thread_id() != 0)
    return;

  auto A = reinterpret_cast<const float*>(arg->A_addr);
  auto B = reinterpret_cast<const float*>(arg->B_addr);
  auto C = reinterpret_cast<float*>(arg->C_addr);

  uint32_t vl;
  __asm__ volatile("vsetvli %[vl], %[avl], e32, m1, ta, ma"
                   : [vl] "=r"(vl)
                   : [avl] "r"(cfg::tileN));

  issue_rvv_tile(A, B);
  store_c_regs(C);
}

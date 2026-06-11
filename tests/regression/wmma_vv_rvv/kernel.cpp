#include "common.h"
#include <tensor_cfg.h>
#include <vx_intrinsics.h>

namespace vt = vortex::tensor;
using cfg = vt::wmma_config_t<NUM_THREADS>;

static_assert(NUM_THREADS == 8,
              "wmma_vv_rvv baseline is fixed to NUM_THREADS=8");
static_assert((kProblemM % cfg::tileM) == 0,
              "wmma_vv_rvv expects problem M divisible by tileM");
static_assert((kProblemN % cfg::tileN) == 0,
              "wmma_vv_rvv expects problem N divisible by tileN");
static_assert((kProblemK % cfg::tileK) == 0,
              "wmma_vv_rvv expects problem K divisible by tileK");

namespace {

static inline void clear_accum_regs() {
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
}

static inline void accum_rvv_tile(const float* A, uint32_t a_row_stride,
                                  const float* B, uint32_t b_row_stride) {
  for (uint32_t k = 0; k < cfg::tileK; ++k) {
    const float* brow = B + k * b_row_stride;
    float a0 = A[0 * a_row_stride + k];
    float a1 = A[1 * a_row_stride + k];
    float a2 = A[2 * a_row_stride + k];
    float a3 = A[3 * a_row_stride + k];
    float a4 = A[4 * a_row_stride + k];
    float a5 = A[5 * a_row_stride + k];
    float a6 = A[6 * a_row_stride + k];
    float a7 = A[7 * a_row_stride + k];

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

static inline void store_c_regs(float* C, uint32_t row_stride) {
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
      : [c0] "r"(C + 0 * row_stride),
        [c1] "r"(C + 1 * row_stride),
        [c2] "r"(C + 2 * row_stride),
        [c3] "r"(C + 3 * row_stride),
        [c4] "r"(C + 4 * row_stride),
        [c5] "r"(C + 5 * row_stride),
        [c6] "r"(C + 6 * row_stride),
        [c7] "r"(C + 7 * row_stride)
      : "memory");
}

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

  for (uint32_t bm = 0; bm < kProblemM; bm += cfg::tileM) {
    for (uint32_t bn = 0; bn < kProblemN; bn += cfg::tileN) {
      clear_accum_regs();

      for (uint32_t bk = 0; bk < kProblemK; bk += cfg::tileK) {
        const float* a_tile = A + bm * kProblemK + bk;
        const float* b_tile = B + bk * kProblemN + bn;

        accum_rvv_tile(a_tile, kProblemK, b_tile, kProblemN);
      }

      store_c_regs(C + bm * kProblemN + bn, kProblemN);
    }
  }
}

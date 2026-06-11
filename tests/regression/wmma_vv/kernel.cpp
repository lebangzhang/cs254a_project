#include "common.h"
#include <tensor_cfg.h>
#include <vx_intrinsics.h>

namespace vt = vortex::tensor;
using cfg = vt::wmma_config_t<NUM_THREADS>;

static_assert(NUM_THREADS == 8,
              "wmma_vv regression is fixed to NUM_THREADS=8");
static_assert(cfg::tileK <= cfg::tileM,
              "wmma_vv regression assumes A rows dominate the source register footprint");
static_assert((kMatrixSize % cfg::tileM) == 0,
              "wmma_vv matrix M must be a multiple of tileM");
static_assert((kMatrixSize % cfg::tileN) == 0,
              "wmma_vv matrix N must be a multiple of tileN");
static_assert((kMatrixSize % cfg::tileK) == 0,
              "wmma_vv matrix K must be a multiple of tileK");

namespace {

constexpr uint32_t kMaxVregs = 32;
constexpr uint32_t kSrcABase = 0;
constexpr uint32_t kSrcBBase = kSrcABase + cfg::tileM;
constexpr uint32_t kUniqueDstBase = kSrcBBase + cfg::tileK;
constexpr uint32_t kDstBase  = (kUniqueDstBase + cfg::tileM <= kMaxVregs) ? kUniqueDstBase : kSrcABase;

constexpr uint32_t encode_wmma_vv(uint32_t vd, uint32_t vs1, uint32_t vs2) {
  return (63u << 26) | (1u << 25) | (vs2 << 20) | (vs1 << 15) | (0u << 12) | (vd << 7) | 0x57u;
}

constexpr uint32_t kWmmaInst = encode_wmma_vv(kDstBase, kSrcABase, kSrcBBase);

static_assert((kSrcBBase + cfg::tileK) <= kMaxVregs,
              "wmma_vv regression needs more vector registers for the source tile groups");
static_assert((kDstBase + cfg::tileM) <= kMaxVregs,
              "wmma_vv regression needs a legal destination register group");

static inline void zero_accum_regs() {
  float zero = 0.0f;
  __asm__ volatile(
      "vfmv.v.f v24, %[zero]\n\t"
      "vfmv.v.f v25, %[zero]\n\t"
      "vfmv.v.f v26, %[zero]\n\t"
      "vfmv.v.f v27, %[zero]\n\t"
      "vfmv.v.f v28, %[zero]\n\t"
      "vfmv.v.f v29, %[zero]\n\t"
      "vfmv.v.f v30, %[zero]\n\t"
      "vfmv.v.f v31, %[zero]"
      :
      : [zero] "f"(zero)
      : "memory");
}

static inline void load_a_regs(const float* A, uint32_t ld) {
  __asm__ volatile(
      "vle32.v v0, (%[a0])\n\t"
      "vle32.v v1, (%[a1])\n\t"
      "vle32.v v2, (%[a2])\n\t"
      "vle32.v v3, (%[a3])\n\t"
      "vle32.v v4, (%[a4])\n\t"
      "vle32.v v5, (%[a5])\n\t"
      "vle32.v v6, (%[a6])\n\t"
      "vle32.v v7, (%[a7])"
      :
      : [a0] "r"(A + 0 * ld),
        [a1] "r"(A + 1 * ld),
        [a2] "r"(A + 2 * ld),
        [a3] "r"(A + 3 * ld),
        [a4] "r"(A + 4 * ld),
        [a5] "r"(A + 5 * ld),
        [a6] "r"(A + 6 * ld),
        [a7] "r"(A + 7 * ld)
      : "memory");
}

static inline void load_b_regs(const float* B, uint32_t ld) {
  __asm__ volatile(
      "vle32.v v8,  (%[b0])\n\t"
      "vle32.v v9,  (%[b1])\n\t"
      "vle32.v v10, (%[b2])\n\t"
      "vle32.v v11, (%[b3])\n\t"
      "vle32.v v12, (%[b4])\n\t"
      "vle32.v v13, (%[b5])\n\t"
      "vle32.v v14, (%[b6])\n\t"
      "vle32.v v15, (%[b7])"
      :
      : [b0] "r"(B + 0 * ld),
        [b1] "r"(B + 1 * ld),
        [b2] "r"(B + 2 * ld),
        [b3] "r"(B + 3 * ld),
        [b4] "r"(B + 4 * ld),
        [b5] "r"(B + 5 * ld),
        [b6] "r"(B + 6 * ld),
        [b7] "r"(B + 7 * ld)
      : "memory");
}

static inline void accum_c_regs() {
  __asm__ volatile(
      "vfadd.vv v24, v24, v16\n\t"
      "vfadd.vv v25, v25, v17\n\t"
      "vfadd.vv v26, v26, v18\n\t"
      "vfadd.vv v27, v27, v19\n\t"
      "vfadd.vv v28, v28, v20\n\t"
      "vfadd.vv v29, v29, v21\n\t"
      "vfadd.vv v30, v30, v22\n\t"
      "vfadd.vv v31, v31, v23"
      :
      :
      : "memory");
}

static inline void store_accum_regs(float* C, uint32_t ld) {
  __asm__ volatile(
      "vse32.v v24, (%[c0])\n\t"
      "vse32.v v25, (%[c1])\n\t"
      "vse32.v v26, (%[c2])\n\t"
      "vse32.v v27, (%[c3])\n\t"
      "vse32.v v28, (%[c4])\n\t"
      "vse32.v v29, (%[c5])\n\t"
      "vse32.v v30, (%[c6])\n\t"
      "vse32.v v31, (%[c7])"
      :
      : [c0] "r"(C + 0 * ld),
        [c1] "r"(C + 1 * ld),
        [c2] "r"(C + 2 * ld),
        [c3] "r"(C + 3 * ld),
        [c4] "r"(C + 4 * ld),
        [c5] "r"(C + 5 * ld),
        [c6] "r"(C + 6 * ld),
        [c7] "r"(C + 7 * ld)
      : "memory");
}

static inline void issue_wmma_vv() {
  __asm__ volatile(".4byte %c[inst]" : : [inst] "i"(kWmmaInst) : "memory");
}

} // namespace

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  if (vx_thread_id() != 0)
    return;

  auto A = reinterpret_cast<const float*>(arg->A_addr);
  auto B = reinterpret_cast<const float*>(arg->B_addr);
  auto C = reinterpret_cast<float*>(arg->C_addr);

  constexpr uint32_t ld = kMatrixSize;
  uint32_t vl;
  __asm__ volatile("vsetvli %[vl], %[avl], e32, m1, ta, ma"
                   : [vl] "=r"(vl)
                   : [avl] "r"(cfg::tileK));

  for (uint32_t m = 0; m < kMatrixSize; m += cfg::tileM) {
    for (uint32_t n = 0; n < kMatrixSize; n += cfg::tileN) {
      zero_accum_regs();
      for (uint32_t k = 0; k < kMatrixSize; k += cfg::tileK) {
        load_a_regs(A + m * ld + k, ld);
        load_b_regs(B + k * ld + n, ld);
        issue_wmma_vv();
        accum_c_regs();
      }
      store_accum_regs(C + m * ld + n, ld);
    }
  }
}

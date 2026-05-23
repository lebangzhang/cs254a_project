#include "common.h"
#include <tensor_cfg.h>
#include <vx_intrinsics.h>

namespace vt = vortex::tensor;
using cfg = vt::wmma_config_t<NUM_THREADS>;

static_assert(NUM_THREADS == 4 || NUM_THREADS == 8 || NUM_THREADS == 16,
              "wmma_vv regression currently supports NUM_THREADS=4/8/16");
static_assert(cfg::tileK <= cfg::tileM,
              "wmma_vv regression assumes A rows dominate the source register footprint");

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

#if NUM_THREADS == 4
static inline void load_a_regs(const float* A) {
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
      : [a0] "r"(A + 0 * cfg::tileK),
        [a1] "r"(A + 1 * cfg::tileK),
        [a2] "r"(A + 2 * cfg::tileK),
        [a3] "r"(A + 3 * cfg::tileK),
        [a4] "r"(A + 4 * cfg::tileK),
        [a5] "r"(A + 5 * cfg::tileK),
        [a6] "r"(A + 6 * cfg::tileK),
        [a7] "r"(A + 7 * cfg::tileK)
      : "memory");
}

static inline void load_b_regs(const float* B) {
  __asm__ volatile(
      "vle32.v v8,  (%[b0])\n\t"
      "vle32.v v9,  (%[b1])\n\t"
      "vle32.v v10, (%[b2])\n\t"
      "vle32.v v11, (%[b3])"
      :
      : [b0] "r"(B + 0 * cfg::tileN),
        [b1] "r"(B + 1 * cfg::tileN),
        [b2] "r"(B + 2 * cfg::tileN),
        [b3] "r"(B + 3 * cfg::tileN)
      : "memory");
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
static inline void load_a_regs(const float* A) {
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
      : [a0] "r"(A + 0 * cfg::tileK),
        [a1] "r"(A + 1 * cfg::tileK),
        [a2] "r"(A + 2 * cfg::tileK),
        [a3] "r"(A + 3 * cfg::tileK),
        [a4] "r"(A + 4 * cfg::tileK),
        [a5] "r"(A + 5 * cfg::tileK),
        [a6] "r"(A + 6 * cfg::tileK),
        [a7] "r"(A + 7 * cfg::tileK)
      : "memory");
}

static inline void load_b_regs(const float* B) {
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
      : [b0] "r"(B + 0 * cfg::tileN),
        [b1] "r"(B + 1 * cfg::tileN),
        [b2] "r"(B + 2 * cfg::tileN),
        [b3] "r"(B + 3 * cfg::tileN),
        [b4] "r"(B + 4 * cfg::tileN),
        [b5] "r"(B + 5 * cfg::tileN),
        [b6] "r"(B + 6 * cfg::tileN),
        [b7] "r"(B + 7 * cfg::tileN)
      : "memory");
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
static inline void load_a_regs(const float* A) {
  __asm__ volatile(
      "vle32.v v0,  (%[a0])\n\t"
      "vle32.v v1,  (%[a1])\n\t"
      "vle32.v v2,  (%[a2])\n\t"
      "vle32.v v3,  (%[a3])\n\t"
      "vle32.v v4,  (%[a4])\n\t"
      "vle32.v v5,  (%[a5])\n\t"
      "vle32.v v6,  (%[a6])\n\t"
      "vle32.v v7,  (%[a7])\n\t"
      "vle32.v v8,  (%[a8])\n\t"
      "vle32.v v9,  (%[a9])\n\t"
      "vle32.v v10, (%[a10])\n\t"
      "vle32.v v11, (%[a11])\n\t"
      "vle32.v v12, (%[a12])\n\t"
      "vle32.v v13, (%[a13])\n\t"
      "vle32.v v14, (%[a14])\n\t"
      "vle32.v v15, (%[a15])"
      :
      : [a0]  "r"(A + 0  * cfg::tileK),
        [a1]  "r"(A + 1  * cfg::tileK),
        [a2]  "r"(A + 2  * cfg::tileK),
        [a3]  "r"(A + 3  * cfg::tileK),
        [a4]  "r"(A + 4  * cfg::tileK),
        [a5]  "r"(A + 5  * cfg::tileK),
        [a6]  "r"(A + 6  * cfg::tileK),
        [a7]  "r"(A + 7  * cfg::tileK),
        [a8]  "r"(A + 8  * cfg::tileK),
        [a9]  "r"(A + 9  * cfg::tileK),
        [a10] "r"(A + 10 * cfg::tileK),
        [a11] "r"(A + 11 * cfg::tileK),
        [a12] "r"(A + 12 * cfg::tileK),
        [a13] "r"(A + 13 * cfg::tileK),
        [a14] "r"(A + 14 * cfg::tileK),
        [a15] "r"(A + 15 * cfg::tileK)
      : "memory");
}

static inline void load_b_regs(const float* B) {
  __asm__ volatile(
      "vle32.v v16, (%[b0])\n\t"
      "vle32.v v17, (%[b1])\n\t"
      "vle32.v v18, (%[b2])\n\t"
      "vle32.v v19, (%[b3])\n\t"
      "vle32.v v20, (%[b4])\n\t"
      "vle32.v v21, (%[b5])\n\t"
      "vle32.v v22, (%[b6])\n\t"
      "vle32.v v23, (%[b7])"
      :
      : [b0] "r"(B + 0 * cfg::tileN),
        [b1] "r"(B + 1 * cfg::tileN),
        [b2] "r"(B + 2 * cfg::tileN),
        [b3] "r"(B + 3 * cfg::tileN),
        [b4] "r"(B + 4 * cfg::tileN),
        [b5] "r"(B + 5 * cfg::tileN),
        [b6] "r"(B + 6 * cfg::tileN),
        [b7] "r"(B + 7 * cfg::tileN)
      : "memory");
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

  uint32_t vl;
  __asm__ volatile("vsetvli %[vl], %[avl], e32, m1, ta, ma"
                   : [vl] "=r"(vl)
                   : [avl] "r"(cfg::tileK));

  load_a_regs(A);
  load_b_regs(B);
  issue_wmma_vv();
  store_c_regs(C);
}

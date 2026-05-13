#include "common.h"
#include <tensor_cfg.h>
#include <vx_intrinsics.h>

namespace vt = vortex::tensor;
using cfg = vt::wmma_config_t<NUM_THREADS>;

static_assert(NUM_THREADS == 8, "wmma_vv regression uses fixed v0-v23 register groups");
static_assert(cfg::tileM == 8 && cfg::tileN == 8 && cfg::tileK == 8,
              "wmma_vv regression expects the NUM_THREADS=8 fp32 tile shape");

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

  __asm__ volatile(
      "vle32.v v0,  (%[a0])\n\t"
      "vle32.v v1,  (%[a1])\n\t"
      "vle32.v v2,  (%[a2])\n\t"
      "vle32.v v3,  (%[a3])\n\t"
      "vle32.v v4,  (%[a4])\n\t"
      "vle32.v v5,  (%[a5])\n\t"
      "vle32.v v6,  (%[a6])\n\t"
      "vle32.v v7,  (%[a7])\n\t"
      "vle32.v v8,  (%[b0])\n\t"
      "vle32.v v9,  (%[b1])\n\t"
      "vle32.v v10, (%[b2])\n\t"
      "vle32.v v11, (%[b3])\n\t"
      "vle32.v v12, (%[b4])\n\t"
      "vle32.v v13, (%[b5])\n\t"
      "vle32.v v14, (%[b6])\n\t"
      "vle32.v v15, (%[b7])\n\t"
      ".word 0xfe800857\n\t"
      "vse32.v v16, (%[c0])\n\t"
      "vse32.v v17, (%[c1])\n\t"
      "vse32.v v18, (%[c2])\n\t"
      "vse32.v v19, (%[c3])\n\t"
      "vse32.v v20, (%[c4])\n\t"
      "vse32.v v21, (%[c5])\n\t"
      "vse32.v v22, (%[c6])\n\t"
      "vse32.v v23, (%[c7])"
      :
      : [a0] "r"(A + 0 * cfg::tileK),
        [a1] "r"(A + 1 * cfg::tileK),
        [a2] "r"(A + 2 * cfg::tileK),
        [a3] "r"(A + 3 * cfg::tileK),
        [a4] "r"(A + 4 * cfg::tileK),
        [a5] "r"(A + 5 * cfg::tileK),
        [a6] "r"(A + 6 * cfg::tileK),
        [a7] "r"(A + 7 * cfg::tileK),
        [b0] "r"(B + 0 * cfg::tileN),
        [b1] "r"(B + 1 * cfg::tileN),
        [b2] "r"(B + 2 * cfg::tileN),
        [b3] "r"(B + 3 * cfg::tileN),
        [b4] "r"(B + 4 * cfg::tileN),
        [b5] "r"(B + 5 * cfg::tileN),
        [b6] "r"(B + 6 * cfg::tileN),
        [b7] "r"(B + 7 * cfg::tileN),
        [c0] "r"(C + 0 * cfg::tileN),
        [c1] "r"(C + 1 * cfg::tileN),
        [c2] "r"(C + 2 * cfg::tileN),
        [c3] "r"(C + 3 * cfg::tileN),
        [c4] "r"(C + 4 * cfg::tileN),
        [c5] "r"(C + 5 * cfg::tileN),
        [c6] "r"(C + 6 * cfg::tileN),
        [c7] "r"(C + 7 * cfg::tileN)
      : "memory");
}

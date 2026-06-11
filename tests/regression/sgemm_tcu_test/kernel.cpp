#include "common.h"
#include <tensor_cfg.h>
#include <vx_intrinsics.h>
#include <vx_tensor.h>

namespace vt = vortex::tensor;
using ctx = vt::wmma_context<NUM_THREADS, vt::ITYPE, vt::OTYPE>;

static_assert(NUM_THREADS == 8,
              "sgemm_tcu_baseline is fixed to NUM_THREADS=8");

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
  auto pA = reinterpret_cast<ctx::input_t*>(arg->A_addr);
  auto pB = reinterpret_cast<ctx::input_t*>(arg->B_addr);
  auto pC = reinterpret_cast<ctx::output_t*>(arg->C_addr);

  ctx::fragment_a fragA;
  ctx::fragment_b fragB;
  ctx::fragment_acc fragC;

  ctx::fill_fragment(fragC, 0);
  ctx::load_matrix_sync(fragA, pA, ctx::tileK);
  ctx::load_matrix_sync(fragB, pB, ctx::tileN);
  ctx::mma_sync(fragC, fragA, fragB, fragC);
  ctx::store_matrix_sync(pC, fragC, ctx::tileN);
}

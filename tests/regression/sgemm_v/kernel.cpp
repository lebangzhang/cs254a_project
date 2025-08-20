#include <vx_spawn.h>
#include "vx_print.h"
#include "common.h"

void kernel_body(kernel_arg_t* __UNIFORM__ arg) {
  auto A = reinterpret_cast<float* __restrict>(arg->A_addr);
  auto B = reinterpret_cast<float* __restrict>(arg->B_addr);
  auto C = reinterpret_cast<float* __restrict>(arg->C_addr);
  int size = arg->size;

  int col_tile = blockIdx.x;
  int row = blockIdx.y;

  // Determine maximum vector length
  uint32_t max_vl;
  __asm__ __volatile__("vsetvli %0, zero, e32, m1, ta, ma" : "=r"(max_vl));

  int col_start = col_tile * max_vl;
  if (col_start >= size)
    return; // Early exit

  int remaining_cols = size - col_start;
  uint32_t vl = remaining_cols < max_vl ? remaining_cols : max_vl;

  // Set vector length and initialize accumulator
  __asm__ __volatile__("vsetvli zero, %0, e32, m1, ta, ma" :: "r"(vl));

  // v0 <- 0.0
  __asm__ __volatile__("vfmv.v.f v0, %0" :: "f"(0.0f));

  const float* __restrict pa = A + row * size;          // A[row, 0]
  const float* __restrict pb = B + col_start;           // B[0, col_start]
  // loop over K (rows of B / columns of A)
  for (int e = 0; e < size; ++e) {
    float a = pa[e];                                    // scalar load (A row)
    __asm__ __volatile__(
      "vle32.v v12, (%[pb])\n\t"                        // load B[e, col_start:col_start+vl)
      "vfmacc.vf v0, %[a], v12\n\t"                     // v0 += a * v12
      :: [pb] "r"(pb), [a] "f"(a)
      : "v12", "memory"
    );
    pb += size;                                         // advance to next row of B
  }

  float* __restrict pc = C + row * size + col_start;
  __asm__ __volatile__("vse32.v v0, (%0)" :: "r"(pc) : "memory");
}

int main() {
  kernel_arg_t *arg = (kernel_arg_t *)csr_read(VX_CSR_MSCRATCH);
  return vx_spawn_threads(2, arg->grid_dim, nullptr, (vx_kernel_func_cb)kernel_body, arg);
}

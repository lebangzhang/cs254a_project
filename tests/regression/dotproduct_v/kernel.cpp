#include <vx_print.h>
#include <vx_spawn.h>
#include "common.h"

void kernel_body(kernel_arg_t *__UNIFORM__ arg) {
  auto src0_ptr = reinterpret_cast<TYPE *>(arg->src0_addr);
  auto src1_ptr = reinterpret_cast<TYPE *>(arg->src1_addr);
  auto dst_ptr = reinterpret_cast<TYPE *>(arg->dst_addr);
  auto num_points = arg->num_points;
  auto vec_len_per_thread = arg->vec_len_per_thread;

  uint32_t tid = threadIdx.x + blockIdx.x * blockDim.x;
  uint32_t cacheIndex = threadIdx.x;

  auto cache = reinterpret_cast<TYPE *>(__local_mem(blockDim.x * sizeof(TYPE)));

  uint32_t start_idx = tid * vec_len_per_thread;

  // Partial sum
  float temp = 0;
  uint32_t vl;
  uint32_t index = start_idx;
  for (uint32_t avl = vec_len_per_thread; avl > 0; avl -= (vl)) {

    // 1. Query next vl
    __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma" : [vl] "=r"(vl) : [avl] "r"(avl));

    // 2. Load src0_ptr
    auto curr = &(src0_ptr[index]);
    __asm__ __volatile__("vle32.v v8, (%[i])" ::[i] "r"(curr));

    // 3. Load src1_ptr
    curr = &(src1_ptr[index]);
    __asm__ __volatile__("vle32.v v16, (%[i])" ::[i] "r"(curr));

    // 4. Multiply
    __asm__ __volatile__("vfmul.vv v16, v16, v8");

    // 5. Reduction
    float zero_val = 0.0f;
    __asm__ __volatile__("vfmv.v.f v8, %[z]" ::[z] "f"(zero_val));
    __asm__ __volatile__("vfredsum.vs v8, v16, v8");

    // 6. Increment the sum
    TYPE temp_sum;
    __asm__ __volatile__("vmv.x.s %[o], v8" : [o] "=r"(temp_sum));
    temp += temp_sum;

    // 7. Next index
    index += vl;
  }

  // Store partial sum in shared memory
  cache[cacheIndex] = temp;
  __syncthreads();

  // Block-level reduction
  int i = blockDim.x / 2;
  while (i != 0) {
    if (cacheIndex < i) {
      cache[cacheIndex] += cache[cacheIndex + i];
    }
    __syncthreads();
    i /= 2;
  }

  // First thread in block writes the block's sum
  if (cacheIndex == 0) {
    dst_ptr[blockIdx.x] = cache[0];
  }
}

int main() {
  kernel_arg_t *arg = (kernel_arg_t *)csr_read(VX_CSR_MSCRATCH);
  return vx_spawn_threads(1, arg->grid_dim, arg->block_dim, (vx_kernel_func_cb)kernel_body, arg);
}

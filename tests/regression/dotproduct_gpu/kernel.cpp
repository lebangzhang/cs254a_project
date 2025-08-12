#include <vx_spawn.h>
#include "common.h"
#include <vx_print.h>

void kernel_body(kernel_arg_t* __UNIFORM__ arg) {
	auto src0_ptr = reinterpret_cast<TYPE*>(arg->src0_addr);
	auto src1_ptr = reinterpret_cast<TYPE*>(arg->src1_addr);
	auto dst_ptr  = reinterpret_cast<TYPE*>(arg->dst_addr);
	auto num_points = arg->num_points;
    auto items_per_thread = arg->items_per_thread; 

	uint32_t tid = threadIdx.x + blockIdx.x * blockDim.x;
	uint32_t cacheIndex = threadIdx.x;

	auto cache = reinterpret_cast<TYPE*>(__local_mem(blockDim.x * sizeof(TYPE)));

    uint32_t start_idx = tid;
    uint32_t end_idx   = start_idx + 1;

    // Partial sum 
    float temp = 0;
    for (int idx = start_idx; idx < end_idx; ++idx) {
        temp += src0_ptr[idx] * src1_ptr[idx];
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
	kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
	return vx_spawn_threads(1, arg->grid_dim, arg->block_dim, (vx_kernel_func_cb)kernel_body, arg);
}

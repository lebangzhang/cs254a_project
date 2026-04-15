#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto A     = reinterpret_cast<TYPE*>(arg->src0_addr);
    auto x_old = reinterpret_cast<TYPE*>(arg->src1_addr);
    auto b     = reinterpret_cast<TYPE*>(arg->src2_addr);
    auto x_new = reinterpret_cast<TYPE*>(arg->dst_addr);
    auto n     = arg->size;

    // Each block handles exactly one row i
    uint32_t i  = blockIdx.x;
    uint32_t tx = threadIdx.x;

    if (i >= n)
        return;

    // Shared memory — allocated via lmem_size in vx_start_g
    auto s_data = reinterpret_cast<TYPE*>(__local_mem());

    // 1. Each thread accumulates partial sum over its strided columns
    TYPE partial_sum = 0.0f;
    for (uint32_t j = tx; j < n; j += blockDim.x) {
        if (j != i)
            partial_sum += A[i * n + j] * x_old[j];
    }

    // 2. Store partial sum in shared memory
    s_data[tx] = partial_sum;
    __syncthreads();

    // 3. Tree reduction in shared memory
    for (uint32_t stride = blockDim.x / 2; stride > 0; stride >>= 1) {
        if (tx < stride)
            s_data[tx] += s_data[tx + stride];
        __syncthreads();
    }

    // 4. Thread 0 writes the result
    if (tx == 0)
        x_new[i] = (b[i] - s_data[0]) / A[i * n + i];
}

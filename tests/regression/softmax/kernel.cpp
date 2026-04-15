#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto src0_ptr = reinterpret_cast<TYPE*>(arg->src0_addr);
    auto dst_ptr  = reinterpret_cast<TYPE*>(arg->dst_addr);

    uint32_t num_cols = arg->num_cols;
    uint32_t row      = blockIdx.x;           // one block = one row
    uint32_t tid      = threadIdx.x;          // thread index within block
    uint32_t stride   = blockDim.x;           // threads per block

    uint32_t offset = row * num_cols;

    auto tbuf = reinterpret_cast<TYPE*>(__local_mem());

    // ----------------------------------------------------------------
    // Pass 1: each thread finds max over its strided columns
    // ----------------------------------------------------------------
    TYPE local_max = 0.0f;
    for (uint32_t i = tid; i < num_cols; i += stride) {
        TYPE v = src0_ptr[offset + i];
        if (v > local_max) local_max = v;
    }

    tbuf[tid] = local_max;
    for (int d = stride / 2; d > 0; d /= 2) {
        __syncthreads();
        if (tid < d)
            tbuf[tid] = tbuf[tid] > tbuf[tid + d] ? tbuf[tid] : tbuf[tid + d];
    }
    __syncthreads();
    TYPE max = tbuf[0];

    // ----------------------------------------------------------------
    // Pass 2: exp(x - max), accumulate partial sum, store back
    // ----------------------------------------------------------------
    TYPE local_sum = 0.0f;
    for (uint32_t i = tid; i < num_cols; i += stride) {
        TYPE e = exp(src0_ptr[offset + i] - max);
        src0_ptr[offset + i] = e;
        local_sum += e;
    }

    tbuf[tid] = local_sum;
    for (int d = stride / 2; d > 0; d /= 2) {
        __syncthreads();
        if (tid < d)
            tbuf[tid] += tbuf[tid + d];
    }
    __syncthreads();
    TYPE sum = tbuf[0];

    // ----------------------------------------------------------------
    // Pass 3: normalize
    // ----------------------------------------------------------------
    for (uint32_t i = tid; i < num_cols; i += stride)
        dst_ptr[offset + i] = src0_ptr[offset + i] / sum;
}

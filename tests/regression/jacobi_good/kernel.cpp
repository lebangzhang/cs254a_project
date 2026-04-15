#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto At    = reinterpret_cast<TYPE*>(arg->src0_addr);  // A transposed by host
    auto x_old = reinterpret_cast<TYPE*>(arg->src1_addr);
    auto b     = reinterpret_cast<TYPE*>(arg->src2_addr);
    auto x_new = reinterpret_cast<TYPE*>(arg->dst_addr);
    auto n     = arg->size;

    uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;

    // At is transposed: At[j*n + i] == A[i*n + j]
    // Each thread reads At[0*n+i], At[1*n+i], At[2*n+i]...
    // At any step j, all threads read At[j*n+0..n-1] — contiguous, coalesced
    TYPE sum = 0.0f;
    for (uint32_t j = 0; j < n; j++) {
        if (j != i)
            sum += At[j * n + i] * x_old[j];
    }
    x_new[i] = (b[i] - sum) / At[i * n + i];  // diagonal: At[i*n+i] == A[i*n+i]
}

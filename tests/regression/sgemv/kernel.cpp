#include <vx_spawn2.h>
#include "common.h"


extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto A = reinterpret_cast<float*>(arg->A_addr);
    auto x = reinterpret_cast<float*>(arg->x_addr);
    auto y = reinterpret_cast<float*>(arg->y_addr);
    uint32_t M = arg->M, N = arg->N;
 
    int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= (int)M)
        return;
 
    const float* row_A = A + row * N;
    float sum = 0.0f;
    for (int col = 0; col < (int)N; ++col)
        sum += row_A[col] * x[col];
 
    y[row] = sum;
}


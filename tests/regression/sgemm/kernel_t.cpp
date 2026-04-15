#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto A  = reinterpret_cast<TYPE*>(arg->A_addr);
    auto Bt = reinterpret_cast<TYPE*>(arg->B_addr);  // B pre-transposed by host
    auto C  = reinterpret_cast<TYPE*>(arg->C_addr);
    auto size = arg->size;

    int col = blockIdx.x * blockDim.x + threadIdx.x;
    int row = blockIdx.y * blockDim.y + threadIdx.y;

    const TYPE* a  = A  + row * size;  // A[row, 0..size)  — sequential over e
    const TYPE* bt = Bt + col * size;  // Bt[col, 0..size) — sequential over e

    TYPE sum(0);
    for (int e = 0; e < (int)size; ++e)
        sum += a[e] * bt[e];

    C[row * size + col] = sum;
}

#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto A    = reinterpret_cast<float* __restrict>(arg->A_addr);
    auto B    = reinterpret_cast<float* __restrict>(arg->B_addr);
    auto C    = reinterpret_cast<float* __restrict>(arg->C_addr);
    int  size = arg->size;

    int col_tile = blockIdx.x * blockDim.x + threadIdx.x;
    int row      = blockIdx.y * blockDim.y + threadIdx.y;

    uint32_t vlen = arg->vlen;

    int col_start = col_tile * (int)vlen;
    if (col_start >= size || row >= size)
        return;

    int remaining = size - col_start;
    uint32_t vl   = ((uint32_t)remaining < vlen) ? (uint32_t)remaining : vlen;
    __asm__ __volatile__("vsetvli zero, %[vl], e32, m1, ta, ma" : : [vl] "r"(vl));

    // Initialize accumulator v0 = 0
    __asm__ __volatile__("vfmv.v.f v0, %[z]" : : [z] "f"(0.0f));

    const float* __restrict pa = A + row * size;   // A[row, 0]
    const float* __restrict pb = B + col_start;    // B[0, col_start]

    for (int e = 0; e < size; ++e) {
        float a = pa[e];
        __asm__ __volatile__(
            "vle32.v v12, (%[pb])\n\t"
            "vfmacc.vf v0, %[a], v12\n\t"
            : : [pb] "r"(pb), [a] "f"(a) : "v12", "memory"
        );
        pb += size;
    }

    float* __restrict pc = C + row * size + col_start;
    __asm__ __volatile__("vse32.v v0, (%[o])" : : [o] "r"(pc) : "memory");
}


#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto B    = reinterpret_cast<float* __restrict>(arg->B_addr);
    auto C    = reinterpret_cast<float* __restrict>(arg->C_addr);
    int  size = arg->size;

    int col_tile = blockIdx.x * blockDim.x + threadIdx.x;

    uint32_t vlen = arg->vlen;

    int col_start = col_tile * (int)vlen;
    if (blockIdx.y != 0 || col_start >= size)
        return;

    int remaining = size - col_start;
    uint32_t vl   = ((uint32_t)remaining < vlen) ? (uint32_t)remaining : vlen;
    float* __restrict out = C + col_start;
    float value = B[0];

    __asm__ __volatile__(
        "vsetvli zero, %[vl], e32, m1, ta, ma\n\t"
        "vfmv.v.f v12, %[value]\n\t"
        "vse32.v v12, (%[out])\n\t"
        :
        : [vl] "r"(vl), [value] "f"(value), [out] "r"(out)
        : "v12", "memory"
    );
}

#include <vx_spawn2.h>
#include <cstdlib>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto src0_ptr = reinterpret_cast<TYPE*>(arg->src0_addr);
    auto src1_ptr = reinterpret_cast<uint32_t*>(arg->src1_addr);
    auto dst_ptr  = reinterpret_cast<TYPE*>(arg->dst_addr);

    auto multiplier = arg->multiplier;

    uint32_t gid = blockIdx.x * blockDim.x + threadIdx.x;
    if (gid >= arg->num_points)
        return;

    uint32_t sel_bit = src1_ptr[gid] & 0x1;
    TYPE scaled_value = src0_ptr[gid] * multiplier;
    dst_ptr[gid] = (sel_bit) ? 0.0f : scaled_value;
}

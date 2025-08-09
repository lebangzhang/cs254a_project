#include <vx_spawn.h>
#include <cstdlib>
#include "common.h"
#include "vx_print.h"

void kernel_body(kernel_arg_t* __UNIFORM__ arg) {
	auto src0_ptr = reinterpret_cast<TYPE*>(arg->src0_addr);
	auto src1_ptr = reinterpret_cast<uint32_t*>(arg->src1_addr);
	auto dst_ptr  = reinterpret_cast<TYPE*>(arg->dst_addr);

	auto multiplier = arg->multiplier;

    uint32_t sel_bit = src1_ptr[blockIdx.x] & 0x1;

	TYPE scaled_value = src0_ptr[blockIdx.x] * multiplier;
	dst_ptr[blockIdx.x] = (sel_bit) ? 0.0 : scaled_value;
}

int main() {
	kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
	return vx_spawn_threads(1, &arg->num_points, nullptr, (vx_kernel_func_cb)kernel_body, arg);
}

#include <vx_spawn.h>
#include "common.h"
#include "vx_print.h"


void kernel_body(kernel_arg_t* __UNIFORM__ arg) {
	auto node = reinterpret_cast<TYPE*>(arg->src0_addr);
	auto edge = reinterpret_cast<TYPE*>(arg->src1_addr);
	auto dst  = reinterpret_cast<TYPE*>(arg->dst_addr);
    auto num_nodes = arg->num_nodes;

    uint32_t tid   = blockIdx.x;
    uint32_t index = tid * 2;       // x2 because each Node has 2 elements
   
    dst[tid] = node[index]; 
}

int main() {
	kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
    return vx_spawn_threads(1, &arg->num_nodes, nullptr, (vx_kernel_func_cb)kernel_body, arg);
}

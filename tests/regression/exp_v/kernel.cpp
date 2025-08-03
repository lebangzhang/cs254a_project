#include <vx_spawn.h>
#include <cstdlib>
#include "common.h"
#include "vx_print.h"


void kernel_body(kernel_arg_t* __UNIFORM__ arg) {
	auto src0_ptr = reinterpret_cast<TYPE*>(arg->src0_addr);
	auto dst_ptr  = reinterpret_cast<TYPE*>(arg->dst_addr);

    auto vec_len_per_thread = arg->vec_len_per_thread;
    auto initial_index = blockIdx.x * vec_len_per_thread;

    // Using instrinsics
    uint32_t vl;
    uint32_t index = initial_index;

    float A[1000];
    for(auto avl = vec_len_per_thread; avl > 0; avl -= (vl)) {

        // 1. Query next vl
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma"
                        : [vl] "=r"(vl)
                        : [avl] "r"(avl));

        // 2. Load into vector registers + calculate bit select

        // Load Input 
        auto a = &(src0_ptr[index]);
        /*
        __asm__ __volatile__("vle32.v v10, (%[i])" ::[i] "r"(a));
    
        // Declare constants 
        float mul1 = 0.25;
        float mul2 = 0.333;
        float mul3 = 0.5;
        float one  = 1.0;
            
        // Perform exp 
        __asm__ __volatile__("vfmul.vf v12, v10, %[scale]" ::[scale] "f"(mul1));    // y = x * 0.25 
        __asm__ __volatile__("vfadd.vf v12, v12, %[scale]" ::[scale] "f"(one));     // y = 1 + y 
    
        __asm__ __volatile__("vfmul.vv v12, v10, v12");                             // y = y * x  
        __asm__ __volatile__("vfmul.vf v12, v12, %[scale]" ::[scale] "f"(mul2));    // y = y * 0.33
        __asm__ __volatile__("vfadd.vf v12, v12, %[scale]" ::[scale] "f"(one));     // y = 1 + y
    
        __asm__ __volatile__("vfmul.vv v12, v10, v12");                             // y = y * x  
        __asm__ __volatile__("vfmul.vf v12, v12, %[scale]" ::[scale] "f"(mul3));    // y = y * 0.5
        __asm__ __volatile__("vfadd.vf v12, v12, %[scale]" ::[scale] "f"(one));     // y = 1 + y
    
        __asm__ __volatile__("vfmul.vv v12, v10, v12");                             // y = y * x  
        __asm__ __volatile__("vfadd.vf v12, v12, %[scale]" ::[scale] "f"(one));     // y = 1 + y
    
    
        auto d2 = &(dst_ptr[index]);
        __asm__ __volatile__("vse32.v v12, (%[o])" ::[o] "r"(d2));
        */ 

        exp_rv_float32(&(src0_ptr[index]), &(dst_ptr[index]));
        
        // 3. Increment index
        index += vl;
    }
}

int main() {
	kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
	return vx_spawn_threads(1, &arg->num_threads_to_run, nullptr, (vx_kernel_func_cb)kernel_body, arg);
}

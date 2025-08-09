#include <vx_spawn.h>
#include <cstdlib>
#include "common.h"
#include "vx_print.h"
#include "riscv_vector.h"

void kernel_body(kernel_arg_t* __UNIFORM__ arg) {
	auto src0_ptr = reinterpret_cast<TYPE*>(arg->src0_addr);
	auto src1_ptr = reinterpret_cast<uint32_t*>(arg->src1_addr);
	auto dst_ptr  = reinterpret_cast<TYPE*>(arg->dst_addr);

	auto multiplier = arg->multiplier;
    auto vec_len_per_thread = arg->vec_len_per_thread;
    auto initial_index = blockIdx.x * vec_len_per_thread;

    // Recognizable
    /*
    auto index = initial_index;
    float A[1000];
    for(auto i = 0; i < vec_len_per_thread; i++){
        
        uint32_t sel_bit = src1_ptr[index] & 0x1;
	    
        TYPE scaled_value = src0_ptr[index] * multiplier;
        A[i] = (sel_bit) ? 0.0 : scaled_value;

        index += 1;
    }

    for(auto i =0; i < vec_len_per_thread; i++){
        dst_ptr[initial_index+i] = A[i];
    }
    */ 

    // Using instrinsics
    uint32_t vl;
    uint32_t index = initial_index;

    uint32_t A[10000];
    float    B[10000];
    for(auto avl = vec_len_per_thread; avl > 0; avl -= (vl)) {

        // 1. Query next vl
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma"
                        : [vl] "=r"(vl)
                        : [avl] "r"(avl));

        // 2. Load into vector registers + calculate bit select  
        auto a = &(src1_ptr[index]);
        __asm__ __volatile__("vle32.v v10, (%[i])" ::[i] "r"(a));
        __asm__ __volatile__("vand.vi v10, v10, 0x1");
        __asm__ __volatile__("vmseq.vi v0, v10, 0x0");


        // Check: 'and' mask produces correct results --> Correct
        /*
        auto d = &(A[0]);
        __asm__ __volatile__("vs2r.v v10, (%[o])" ::[o] "r"(d));

        for(uint32_t i = 0; i < vl; i++) {
            vx_printf("%d %d\n",i ,A[i]);
        }
        */

        // 2. Load into vector register 
        auto b = &(src0_ptr[index]);
        __asm__ __volatile__("vle32.v v8, (%[i])" ::[i] "r"(b));
        
        // Check: if loading input gives correct values --> Correct 
        /*
        auto d2 = &(B[0]);
        __asm__ __volatile__("vs2r.v v8, (%[o])" ::[o] "r"(d2));

        for(uint32_t i = 0; i < vl; i++) {
            vx_printf("%d %f\n",i ,B[i]);
        }
        */

        // 3. Initialize and multiply by 0 
        __asm__ __volatile__("vfmul.vf v24, v8, %[scale], v0.t" ::[scale] "f"(multiplier));

        // Check: if calculation gives correct values --> Correct 
        /*
        auto d2 = &(B[0]);
        __asm__ __volatile__("vse32.v v24, (%[o])" ::[o] "r"(d2));

        for(uint32_t i = 0; i < vl; i++) {
            vx_printf("%d %f\n",i ,B[i]);
        }
        */

        // 4. Store from vector -> dest
        auto d = &(dst_ptr[index]);
        __asm__ __volatile__("vse32.v v24, (%[o])" ::[o] "r"(d));

        // 5. Increment index
        index += vl;
    }
}

int main() {
	kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
	return vx_spawn_threads(1, &arg->num_threads_to_run, nullptr, (vx_kernel_func_cb)kernel_body, arg);
}

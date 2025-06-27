#include <vx_spawn.h>
#include "common.h"
#include <riscv_vector.h>
#include <vx_print.h>

void kernel_body(kernel_arg_t* __UNIFORM__ arg) {
	auto src0_ptr = reinterpret_cast<TYPE*>(arg->src0_addr);
	auto src1_ptr = reinterpret_cast<TYPE*>(arg->src1_addr);
	auto dst_ptr  = reinterpret_cast<TYPE*>(arg->dst_addr);
	
    auto t = arg->num_threads_to_run;
    auto vec_len_per_thread = arg->vec_len_per_thread;

    uint32_t initial_index = blockIdx.x * vec_len_per_thread; 

    // Not Recognizable
    uint32_t index = blockIdx.x * vec_len_per_thread; 


    TYPE* __restrict src0_ptr2 = src0_ptr+index;
    TYPE* __restrict src1_ptr2 = src1_ptr+index;
    TYPE* __restrict dst_ptr2 = dst_ptr+index;

    for(uint32_t i = 0; i < vec_len_per_thread; i++){
        auto a = src0_ptr2[i];
        auto b = src1_ptr2[i];
        dst_ptr2[i] = a + b;
    }

    // Recognizable
    /*
    auto sum = 0.0f;
    for(uint32_t i = 0; i < vec_len_per_thread; i++){

        auto a = src0_ptr[index+i];
        auto b = src1_ptr[index+i];

        sum += a + b;
    }
	dst_ptr[index] = sum;
    */

    // Recognizable
    /*
    TYPE A[10];
    for(uint32_t i = 0; i < vec_len_per_thread; i++){

        auto a = src0_ptr[initial_index+i];
        auto b = src1_ptr[initial_index+i];
        A[i] = a + b;
    }

    for(uint32_t i = 0; i < vec_len_per_thread; i++){
        dst_ptr[initial_index+i] = A[i];
    }
    */

    // Attempt ??? 
    /*
    TYPE A[10];
    for(uint32_t i = 0; i < vec_len_per_thread; i++){

        auto a = src0_ptr[initial_index+i];
        auto b = src1_ptr[initial_index+i];
        A[i] = a + b;
    }
    */
    /*
    auto avl = 1;
    uint32_t vl;
    uint32_t A_start_index = 0;
    for(; avl > 0; avl -= vl){
        
        // 1. Query next vl 
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m8, ta, ma"
                        : [vl] "=r"(vl)
                        : [avl] "r"(avl));

        // 2. Load into vector registers
        auto a = &(A[A_start_index]);
        __asm__ __volatile__("vle32.v v8, (%[i])" ::[i] "r"(a));

        // 3. Store 
        auto d = &(dst_ptr[A_start_index + initial_index]);
        __asm__ __volatile__("vle32.v v8, (%[i])" ::[i] "r"(d));

        // 4. Update Pointers
        A_start_index += vl;

    }
    */


    // Using instrinsics
    /*
    uint32_t vl;
    uint32_t index = initial_index;

    for(auto avl = vec_len_per_thread; avl > 0; avl -= (vl)) {

        // 1. Query next vl 
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m8, ta, ma"
                        : [vl] "=r"(vl)
                        : [avl] "r"(avl));

        // 2. Load into vector registers
        auto a = &(src0_ptr[index]);
        __asm__ __volatile__("vle32.v v7, (%[i])" ::[i] "r"(a));
        auto b = &(src1_ptr[index]);
        __asm__ __volatile__("vle32.v v10, (%[i])" ::[i] "r"(b));

        // 3. Add Operation
        __asm__ __volatile__("vadd.vv v7, v7, v10"); 

        // 4. Store from vector -> dest 
        auto d = &(dst_ptr[index]);
        __asm__ __volatile__("vse32.v v7, (%[o])" ::[o] "r"(d));
        
        // 5. Increment index
        index += vl;
    }
    */

    // NEED TO FIX THE OTHER 'GENERAL CASE + FLOAT' REQUIREMENETS
}

int main() {
	kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
	return vx_spawn_threads(1, &arg->num_threads_to_run, nullptr, (vx_kernel_func_cb)kernel_body, arg);
	/*return vx_spawn_threads(1, 1, nullptr, (vx_kernel_func_cb)kernel_body, arg);*/
}

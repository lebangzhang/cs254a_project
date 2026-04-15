#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto src0_ptr = reinterpret_cast<TYPE*>(arg->src0_addr);
    auto src1_ptr = reinterpret_cast<TYPE*>(arg->src1_addr);
    auto dst_ptr  = reinterpret_cast<TYPE*>(arg->dst_addr);

    /*auto vec_len_per_thread = arg->vec_len_per_thread;*/
    auto vec_len_per_thread = 4;

    uint32_t index = (blockIdx.x * blockDim.x + threadIdx.x) * vec_len_per_thread;

    /*uint32_t avl = 4;*/
    uint32_t vl;
    /*, vl = 4;*/
    /*for (auto avl = vec_len_per_thread; avl > 0; avl -= vl) {*/

        uint32_t avl = 4; 
        // 1. Query next vl
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma"
                        : [vl] "=r"(vl)
                        : [avl] "r"(avl));

        // 2. Load src0[index..index+vl) into v7
        auto a = &(src0_ptr[index]);
        __asm__ __volatile__("vle32.v v7, (%[i])" : : [i] "r"(a) : "memory");

        // 3. Load src1[index..index+vl) into v10
        auto b = &(src1_ptr[index]);
        __asm__ __volatile__("vle32.v v10, (%[i])" : : [i] "r"(b) : "memory");

        // 4. Float add: v7 = v7 + v10
        __asm__ __volatile__("vfadd.vv v7, v7, v10");

        // 5. Store v7 -> dst[index..index+vl)
        auto d = &(dst_ptr[index]);
        __asm__ __volatile__("vse32.v v7, (%[o])" : : [o] "r"(d) : "memory");

        // 6. Advance
        /*index += vl;*/
    /*}*/
}

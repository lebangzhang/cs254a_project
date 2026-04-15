#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto src0_ptr = reinterpret_cast<TYPE*>(arg->src0_addr);
    auto src1_ptr = reinterpret_cast<uint32_t*>(arg->src1_addr);
    auto dst_ptr  = reinterpret_cast<TYPE*>(arg->dst_addr);

    auto multiplier         = arg->multiplier;
    auto vec_len_per_thread = arg->vec_len_per_thread;

    uint32_t index = (blockIdx.x * blockDim.x + threadIdx.x) * vec_len_per_thread;

    uint32_t vl;
    for (auto avl = vec_len_per_thread; avl > 0; avl -= vl) {

        // 1. Query next vl
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma"
                        : [vl] "=r"(vl)
                        : [avl] "r"(avl));

        // 2. Load mask, extract LSB, set v0 where bit == 0 (keep)
        auto a = &(src1_ptr[index]);
        __asm__ __volatile__("vle32.v v10, (%[i])"    : : [i] "r"(a) : "memory");
        __asm__ __volatile__("vand.vi  v10, v10, 0x1");
        __asm__ __volatile__("vmseq.vi v0,  v10, 0x0");

        // 3. Load input values into v8
        auto b = &(src0_ptr[index]);
        __asm__ __volatile__("vle32.v v8, (%[i])" : : [i] "r"(b) : "memory");

        // 4. Masked multiply: dst = src * multiplier where mask==0, else 0
        __asm__ __volatile__("vfmul.vf v24, v8, %[scale], v0.t"
                        : : [scale] "f"(multiplier));

        // 5. Store result
        auto d = &(dst_ptr[index]);
        __asm__ __volatile__("vse32.v v24, (%[o])" : : [o] "r"(d) : "memory");

        index += vl;
    }
}

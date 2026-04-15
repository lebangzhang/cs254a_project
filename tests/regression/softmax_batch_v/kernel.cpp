#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto src0_ptr = reinterpret_cast<TYPE*>(arg->src0_addr);
    auto dst_ptr  = reinterpret_cast<TYPE*>(arg->dst_addr);

    uint32_t num_cols = arg->num_cols;
    uint32_t row      = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= arg->num_rows)
        return;

    uint32_t offset = row * num_cols;
    uint32_t vl;

    // ----------------------------------------------------------------
    // Pass 1: find max across the row
    // ----------------------------------------------------------------
    TYPE max = 0.0f;
    uint32_t index = offset;
    for (auto avl = num_cols; avl > 0; avl -= vl) {
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma"
                        : [vl] "=r"(vl) : [avl] "r"(avl));

        auto curr = &(src0_ptr[index]);
        __asm__ __volatile__("vle32.v v10, (%[i])" : : [i] "r"(curr) : "memory");

        // Reduce to find max of this chunk (seed with itself)
        __asm__ __volatile__("vfredmax.vs v16, v10, v10");

        uint32_t temp_bits;
        __asm__ __volatile__("vmv.x.s %[o], v16" : [o] "=r"(temp_bits));
        TYPE temp_max;
        __builtin_memcpy(&temp_max, &temp_bits, sizeof(TYPE));
        if (temp_max > max) max = temp_max;

        index += vl;
    }

    // ----------------------------------------------------------------
    // Pass 2: subtract max, compute exp, accumulate sum, store back
    // ----------------------------------------------------------------
    TYPE sum = 0.0f;
    float mul1 = 0.25f, mul2 = 0.333f, mul3 = 0.5f, one = 1.0f;
    float zero_val = 0.0f;

    index = offset;
    for (auto avl = num_cols; avl > 0; avl -= vl) {
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma"
                        : [vl] "=r"(vl) : [avl] "r"(avl));

        auto curr = &(src0_ptr[index]);
        __asm__ __volatile__("vle32.v v10, (%[i])" : : [i] "r"(curr) : "memory");

        // Subtract max
        __asm__ __volatile__("vfmv.v.f v16, %[max]" : : [max] "f"(max));
        __asm__ __volatile__("vfsub.vv v10, v10, v16");

        // Compute exp(v10) -> v12
        __asm__ __volatile__("vfmul.vf v12, v10, %[s]" : : [s] "f"(mul1));   // y = x*0.25
        __asm__ __volatile__("vfadd.vf v12, v12, %[s]" : : [s] "f"(one));    // y = 1+y
        __asm__ __volatile__("vfmul.vv v12, v10, v12");                       // y = x*y
        __asm__ __volatile__("vfmul.vf v12, v12, %[s]" : : [s] "f"(mul2));   // y = y*0.333
        __asm__ __volatile__("vfadd.vf v12, v12, %[s]" : : [s] "f"(one));    // y = 1+y
        __asm__ __volatile__("vfmul.vv v12, v10, v12");                       // y = x*y
        __asm__ __volatile__("vfmul.vf v12, v12, %[s]" : : [s] "f"(mul3));   // y = y*0.5
        __asm__ __volatile__("vfadd.vf v12, v12, %[s]" : : [s] "f"(one));    // y = 1+y
        __asm__ __volatile__("vfmul.vv v12, v10, v12");                       // y = x*y
        __asm__ __volatile__("vfadd.vf v12, v12, %[s]" : : [s] "f"(one));    // y = 1+y

        // Store exp values back into src0 for pass 3
        __asm__ __volatile__("vse32.v v12, (%[o])" : : [o] "r"(curr) : "memory");

        // Reduce sum
        __asm__ __volatile__("vfmv.v.f v8, %[z]" : : [z] "f"(zero_val));
        __asm__ __volatile__("vfredsum.vs v8, v12, v8");

        uint32_t temp_bits;
        __asm__ __volatile__("vmv.x.s %[o], v8" : [o] "=r"(temp_bits));
        TYPE temp_sum;
        __builtin_memcpy(&temp_sum, &temp_bits, sizeof(TYPE));
        sum += temp_sum;

        index += vl;
    }

    // ----------------------------------------------------------------
    // Pass 3: normalize
    // ----------------------------------------------------------------
    index = offset;
    for (auto avl = num_cols; avl > 0; avl -= vl) {
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma"
                        : [vl] "=r"(vl) : [avl] "r"(avl));

        auto curr = &(src0_ptr[index]);
        __asm__ __volatile__("vle32.v v10, (%[i])" : : [i] "r"(curr) : "memory");

        __asm__ __volatile__("vfmv.v.f v16, %[s]" : : [s] "f"(sum));
        __asm__ __volatile__("vfdiv.vv v24, v10, v16");

        auto d = &(dst_ptr[index]);
        __asm__ __volatile__("vse32.v v24, (%[o])" : : [o] "r"(d) : "memory");

        index += vl;
    }
}

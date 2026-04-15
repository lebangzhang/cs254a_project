#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto A     = reinterpret_cast<TYPE*>(arg->src0_addr);
    auto x_old = reinterpret_cast<TYPE*>(arg->src1_addr);
    auto b     = reinterpret_cast<TYPE*>(arg->src2_addr);
    auto x_new = reinterpret_cast<TYPE*>(arg->dst_addr);
    auto n     = arg->size;

    uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n)
        return;

    uint32_t A_index = i * n;
    uint32_t x_index = 0;

    float sum = 0.0f;
    uint32_t vl;

    // Initialize accumulator v16 = 0
    float zero = 0.0f;
    __asm__ __volatile__("vsetvli zero, %[n], e32, m1, ta, ma" : : [n] "r"(n));
    __asm__ __volatile__("vfmv.v.f v16, %[z]" : : [z] "f"(zero));

    for (auto avl = n; avl > 0; avl -= vl) {

        // 1. Query next vl
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma"
                        : [vl] "=r"(vl) : [avl] "r"(avl));

        // 2. Load A[i, A_index..+vl)
        auto pa = &A[A_index];
        __asm__ __volatile__("vle32.v v10, (%[i])" : : [i] "r"(pa) : "memory");

        // 3. Load x_old[x_index..+vl)
        auto px = &x_old[x_index];
        __asm__ __volatile__("vle32.v v14, (%[i])" : : [i] "r"(px) : "memory");

        // 4. Accumulate: v16 += v10 * v14
        __asm__ __volatile__("vfmacc.vv v16, v14, v10");

        A_index += vl;
        x_index += vl;
    }

    // 5. Reduce v16 to scalar sum
    __asm__ __volatile__("vsetvli zero, %[n], e32, m1, ta, ma" : : [n] "r"(n));
    __asm__ __volatile__("vfmv.v.f v8, %[z]" : : [z] "f"(zero));
    __asm__ __volatile__("vfredsum.vs v8, v16, v8");

    uint32_t sum_bits;
    __asm__ __volatile__("vmv.x.s %[o], v8" : [o] "=r"(sum_bits));
    __builtin_memcpy(&sum, &sum_bits, sizeof(float));

    // 6. Subtract diagonal term (was included in dot product)
    sum -= A[i * n + i] * x_old[i];

    x_new[i] = (b[i] - sum) / A[i * n + i];
}

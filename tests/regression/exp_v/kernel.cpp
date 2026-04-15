#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto A = reinterpret_cast<float*>(arg->A_addr);
    auto x = reinterpret_cast<float*>(arg->x_addr);
    auto y = reinterpret_cast<float*>(arg->y_addr);
    uint32_t M = arg->M;
    uint32_t N = arg->N;

    int row = blockIdx.x * blockDim.x + threadIdx.x;
    if (row >= (int)M)
        return;

    uint32_t index_a = row * N;
    uint32_t index_b = 0;
    float sum = 0.0f;
    uint32_t vl;

    // Initialize accumulator v8 = 0
    float zero = 0.0f;
    __asm__ __volatile__("vfmv.v.f v8, %[z]" : : [z] "f"(zero));

    for (auto avl = N; avl > 0; avl -= vl) {

        // 1. Query next vl
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma"
                        : [vl] "=r"(vl) : [avl] "r"(avl));

        // 2. Load A[row, index_a..+vl)
        auto a = &A[index_a];
        __asm__ __volatile__("vle32.v v10, (%[i])" : : [i] "r"(a) : "memory");

        // 3. Load x[index_b..+vl)
        auto b = &x[index_b];
        __asm__ __volatile__("vle32.v v12, (%[i])" : : [i] "r"(b) : "memory");

        // 4. Element-wise multiply: v12 = v10 * v12
        __asm__ __volatile__("vfmul.vv v12, v10, v12");

        // 5. Reduce into running sum: v8[0] += sum(v12)
        __asm__ __volatile__("vfredsum.vs v8, v12, v8");

        index_a += vl;
        index_b += vl;
    }

    // 6. Extract scalar result — use vmv.x.s + builtin_memcpy (float bits)
    uint32_t sum_bits;
    __asm__ __volatile__("vmv.x.s %[o], v8" : [o] "=r"(sum_bits));
    __builtin_memcpy(&sum, &sum_bits, sizeof(float));

    y[row] = sum;
}

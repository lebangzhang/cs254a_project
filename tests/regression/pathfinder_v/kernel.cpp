#include <vx_spawn2.h>
#include "common.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto wall             = reinterpret_cast<TYPE*>(arg->src0_addr);
    auto src              = reinterpret_cast<TYPE*>(arg->src1_addr);
    auto dst              = reinterpret_cast<TYPE*>(arg->dst_addr);
    auto num_cols         = arg->num_cols;
    auto vec_len_per_thread = arg->vec_len_per_thread;

    uint32_t n     = (blockIdx.x * blockDim.x + threadIdx.x) * vec_len_per_thread;
    uint32_t index = n;
    uint32_t vl;

    for (auto avl = vec_len_per_thread; avl > 0; avl -= vl) {

        // 1. Query next vl
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma"
                        : [vl] "=r"(vl)
                        : [avl] "r"(avl));

        // 2. Load src (previous row's accumulated costs) into v10
        auto a = &(src[index]);
        __asm__ __volatile__("vle32.v v10, (%[i])" : : [i] "r"(a) : "memory");

        // 3. Slide neighbours: left boundary and right boundary
        auto aux  = (n == 0)             ? src[0]          : src[n - 1];
        auto aux2 = (n + vl >= num_cols) ? src[n + vl - 1] : src[n + vl];

        __asm__ __volatile__("vslide1up.vx   v12, v10, %[i]" : : [i] "r"(aux));
        __asm__ __volatile__("vslide1down.vx v14, v10, %[i]" : : [i] "r"(aux2));

        // 4. Min across centre, left, right
        __asm__ __volatile__("vmin.vv v10, v10, v12");
        __asm__ __volatile__("vmin.vv v10, v10, v14");

        // 5. Load current wall row into v12
        auto b = &(wall[index]);
        __asm__ __volatile__("vle32.v v12, (%[i])" : : [i] "r"(b) : "memory");

        // 6. Add wall cost
        __asm__ __volatile__("vadd.vv v10, v10, v12");

        // 7. Store result to dst
        auto c = &(dst[index]);
        __asm__ __volatile__("vse32.v v10, (%[o])" : : [o] "r"(c) : "memory");

        index += vl;
    }
}


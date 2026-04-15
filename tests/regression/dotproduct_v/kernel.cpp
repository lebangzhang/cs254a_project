#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    auto src0_ptr = reinterpret_cast<TYPE*>(arg->src0_addr);
    auto src1_ptr = reinterpret_cast<TYPE*>(arg->src1_addr);
    auto dst_ptr  = reinterpret_cast<TYPE*>(arg->dst_addr);
    auto num_points         = arg->num_points;
    auto vec_len_per_thread = arg->vec_len_per_thread;

    int tid        = threadIdx.x;
    int gid        = blockIdx.x * blockDim.x + tid;
    uint32_t stride = blockDim.x * gridDim.x;

    auto tbuf = reinterpret_cast<TYPE*>(__local_mem());

    // Each thread accumulates a partial sum over its RVV chunk(s)
    float temp = 0.0f;
    uint32_t vl;

    for (uint32_t i = gid; i < num_points / vec_len_per_thread; i += stride) {
        uint32_t index = i * vec_len_per_thread;

        for (uint32_t avl = vec_len_per_thread; avl > 0; avl -= vl) {

            // 1. Query next vl
            __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma"
                            : [vl] "=r"(vl)
                            : [avl] "r"(avl));

            // 2. Load src0 into v8
            auto a = &(src0_ptr[index]);
            __asm__ __volatile__("vle32.v v8, (%[i])" : : [i] "r"(a) : "memory");

            // 3. Load src1 into v16
            auto b = &(src1_ptr[index]);
            __asm__ __volatile__("vle32.v v16, (%[i])" : : [i] "r"(b) : "memory");

            // 4. Element-wise multiply: v16 = v8 * v16
            __asm__ __volatile__("vfmul.vv v16, v16, v8");

            // 5. Reduce into scalar: v8[0] = sum(v16)
            float zero_val = 0.0f;
            __asm__ __volatile__("vfmv.v.f v8, %[z]" : : [z] "f"(zero_val));
            __asm__ __volatile__("vfredsum.vs v8, v16, v8");

            // 6. Extract scalar and accumulate (use integer register, reinterpret bits)
            uint32_t temp_bits;
            __asm__ __volatile__("vmv.x.s %[o], v8" : [o] "=r"(temp_bits));
            TYPE temp_sum;
            __builtin_memcpy(&temp_sum, &temp_bits, sizeof(TYPE));
            temp += temp_sum;

            index += vl;
        }
    }

    // Store partial sum in shared (local) memory
    tbuf[tid] = temp;

    // Block-level tree reduction
    for (int d = blockDim.x / 2; d > 0; d /= 2) {
        __syncthreads();
        if (tid < d) {
            tbuf[tid] += tbuf[tid + d];
        }
    }

    // First thread in block writes block result
    if (tid == 0) {
        dst_ptr[blockIdx.x] = tbuf[0];
    }
}

#include <vx_spawn.h>
#include "common.h"
#include "vx_print.h"


void kernel_body(kernel_arg_t* __UNIFORM__ arg) {
	auto A     = reinterpret_cast<TYPE*>(arg->src0_addr);
	auto x_old = reinterpret_cast<TYPE*>(arg->src1_addr);
	auto b     = reinterpret_cast<TYPE*>(arg->src2_addr);
	auto x_new = reinterpret_cast<TYPE*>(arg->dst_addr);
    auto n     = arg->size; 

    uint32_t i     = blockIdx.x;
    uint32_t A_index = i * n; 
    uint32_t b_index = 0; 

    float sum = 0.0;
    uint32_t avl, vl;

    for(auto avl = n; avl > 0; avl -= (vl)) {

        // 1. Query next vl
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma" : [vl] "=r"(vl) : [avl] "r"(avl));

        // 2. Load A 
        auto a = &(A[A_index]);
        __asm__ __volatile__("vle32.v v10, (%[i])" ::[i] "r"(a)); 

        // 3. Load x_old 
        auto b = &(x_old[b_index]);
        __asm__ __volatile__("vle32.v v14, (%[i])" ::[i] "r"(b));  

        // 4. Mac 
        __asm__ __volatile__("vfmacc.vv v16, v14, v10"); 

        // 5. Reduction 
        float zero_val = 0.0f;
        __asm__ __volatile__("vfmv.v.f v8, %[z]" :: [z] "f"(zero_val));
        __asm__ __volatile__("vfredsum.vs v8, v16, v8");

        TYPE temp_sum;
        __asm__ __volatile__("vmv.x.s %[o], v8" : [o] "=r"(temp_sum));
        sum += temp_sum;

        A_index += vl;
        b_index += vl;

    }
    sum -= A[i*n + i] * x_old[i];


    x_new[i] = (b[i] - sum) / A[i*n + i];
}

int main() {
	kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
    return vx_spawn_threads(1, &arg->size, nullptr, (vx_kernel_func_cb)kernel_body, arg);
}

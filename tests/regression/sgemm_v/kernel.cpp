#include <vx_spawn.h>
#include "common.h"
#include "vx_print.h"

void kernel_body(kernel_arg_t* __UNIFORM__ arg) {
	auto A = reinterpret_cast<TYPE*>(arg->A_addr);
	auto B = reinterpret_cast<TYPE*>(arg->B_addr);
	auto C = reinterpret_cast<TYPE*>(arg->C_addr);
    auto size = arg->size;

    int col = blockIdx.x;
    int row = blockIdx.y;

    TYPE sum(0);
    /*for (int e = 0; e < size; ++e) {*/
    /*    sum += A[row * size + e] * B[e * size + col];*/
    /*}*/
    
    uint32_t vl;
    uint32_t index_a = row * size;
    uint32_t index_b = 0; 
    uint32_t stride_b = size * 4;
    uint32_t temp = 16;

    for(auto avl = size; avl > 0; avl -= (vl)) {
        
        // 0. Query 
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma"
                : [vl] "=r"(vl)
                : [avl] "r"(temp));

        // 1. Load A 
        auto a = &(A[index_a]); 
        __asm__ __volatile__("vle32.v v10, (%[i])" ::[i] "r"(a));


        // 2. Load B 
        auto b = &(B[index_b*size + col]); 
        __asm__ __volatile__("vlse32.v v12, (%[pb]), %[stride]"
            :: [pb] "r"(b), [stride] "r"(stride_b));
        

        // 3. Vector Add 
        __asm__ __volatile__("vfmul.vv v12, v10, v12");


        /*// 4. Reduction */
        float zero_val = 0.0f;
        __asm__ __volatile__("vfmv.v.f v8, %[z]" :: [z] "f"(zero_val));
        __asm__ __volatile__("vfredsum.vs v8, v12, v8");




        // 5 Do the sum     
        TYPE temp_sum;
        __asm__ __volatile__("vmv.x.s %[o], v8" : [o] "=r"(temp_sum));
        sum += temp_sum;

        // 5. Next Index
        index_a += vl; 
        index_b += vl; 
    }

    C[row * size + col] = sum;
}

int main() {
	kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
	return vx_spawn_threads(2, arg->grid_dim, nullptr, (vx_kernel_func_cb)kernel_body, arg);
}

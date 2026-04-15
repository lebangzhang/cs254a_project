#include <vx_spawn.h>
#include "common.h"
#include "vx_print.h"

#define MIN(a, b) ((a) < (b) ? (a) : (b))


void kernel_body(kernel_arg_t* __UNIFORM__ arg) {
	auto wall = reinterpret_cast<TYPE*>(arg->src0_addr);
	auto src  = reinterpret_cast<TYPE*>(arg->src1_addr);
	auto dst  = reinterpret_cast<TYPE*>(arg->dst_addr);
    auto num_cols = arg->num_cols; 
    auto num_rows = arg->num_rows; 
    auto vec_len_per_thread = arg->vec_len_per_thread;

    TYPE min;
    TYPE* temp;
    uint32_t n = blockIdx.x * vec_len_per_thread;
    uint32_t index_old;


    uint32_t initial_index = blockIdx.x * vec_len_per_thread;
    uint32_t index = initial_index;
    uint32_t vl;

    // Due to pointer flipping, and num_rows is even ==> we need to 'flip' src and dst from the original case
    // If increase points but not increase threads then bug
    // But if increase point but increase threads also ==> Bug

    // 0. Initialization 
    for(auto avl = vec_len_per_thread; avl > 0; avl -= (vl)) {

        // 1. Query next vl
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma" : [vl] "=r"(vl) : [avl] "r"(avl));

        // 2. Load and Store to dest
        auto a = &(wall[index]);
        auto b = &(src[index]);
        __asm__ __volatile__("vle32.v v10, (%[i])" ::[i] "r"(a));
        __asm__ __volatile__("vse32.v v10, (%[o])" ::[o] "r"(b));
        index += vl;
    }

    // Debug Printing 
    // 1. Pathfinding
    for(uint32_t i = 0; i < num_rows - 1; i++){

        // 1. Sync + Pointer Swap 
        __syncthreads();
        temp = dst;
        dst  = src;
        src  = temp;

        index = initial_index;
        for(auto avl = vec_len_per_thread; avl > 0; avl -= (vl)) {

            // 1. Query next vl
            __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma" : [vl] "=r"(vl) : [avl] "r"(avl));

            // 2. Load min value 
            auto a = &(dst[index]);
            __asm__ __volatile__("vle32.v v10, (%[i])" ::[i] "r"(a));


            // 3. Slide aux replacements (Note: aux are at 'n' granualrity, not 'n+vl') 
            auto aux =  (n == 0) ? dst[0] : dst[n - 1] ;
            auto aux2 = (n + vl >= num_cols) ? dst[n + vl - 1] : dst[n + vl]; 

            __asm__ __volatile__("vslide1up.vx v12, v10, %[i]" ::[i] "r"(aux));
            __asm__ __volatile__("vslide1down.vx v14, v10, %[i]" ::[i] "r"(aux2));

            // 4. Get Minimum 
            __asm__ __volatile__("vmin.vv v10, v10, v12");
            __asm__ __volatile__("vmin.vv v10, v10, v14");

            // 5. Load Wall 
            auto b = &(wall[(i + 1) * num_cols + index]); 
            __asm__ __volatile__("vle32.v v12, (%[i])" ::[i] "r"(b));

            // 6. Add 
            __asm__ __volatile__("vadd.vv v10, v10, v12");

            // 7. Store back 
            auto c = &(src[index]); 
            __asm__ __volatile__("vse32.v v10, (%[o])" ::[o] "r"(c));

            // Debug 
            /*int A[1000];*/
            /*__asm__ __volatile__("vse32.v v10, (%[o])" ::[o] "r"(&A));*/
            /**/
            /*vx_printf("%d\n", A[0]);*/
            /*vx_printf("%d\n", A[1]);*/

            index += vl;
        }
    }
}

int main() {
	kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
    return vx_spawn_threads(1, &arg->num_threads_to_run, nullptr, (vx_kernel_func_cb)kernel_body, arg);
}

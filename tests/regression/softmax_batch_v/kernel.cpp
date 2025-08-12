#include <vx_spawn.h>
#include "common.h"
#include "vx_print.h"

void kernel_body(kernel_arg_t* __UNIFORM__ arg) {
	auto src0_ptr = reinterpret_cast<TYPE*>(arg->src0_addr);
	auto src1_ptr = reinterpret_cast<TYPE*>(arg->src1_addr);
	auto dst_ptr  = reinterpret_cast<TYPE*>(arg->dst_addr);

    uint32_t num_cols = arg->num_cols;

    uint32_t tid    = blockIdx.x;
    uint32_t offset = blockIdx.x * num_cols;

    uint32_t avl = num_cols;
    uint32_t vl;

    // 1. Find the max value in src0_ptr 
    TYPE max = 0.0;
    uint32_t index = offset;

    for(auto avl = num_cols; avl > 0; avl -= (vl)) {
        // 1. Query next vl
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma" : [vl] "=r"(vl) : [avl] "r"(avl));

        // 2. Load src0_ptr to get maximum 
        auto curr = &(src0_ptr[index]); 
        __asm__ __volatile__("vle32.v v10, (%[i])" ::[i] "r"(curr));

        // 3. Reduction to get maximum 
        __asm__ __volatile__("vfredmax.vs v16, v10, v10");
        
        // 4. Store maximum of this in temp_max 
        TYPE temp_max;
        __asm__ __volatile__("vmv.x.s %[o], v16" : [o] "=r"(temp_max));

        // 5. Update max 
        max = (temp_max > max) ? temp_max : max;

        index += vl;
    }


    // 2. Get the Sum (denominator )
    TYPE sum = 0.0;
    index = offset;
    for(auto avl = num_cols; avl > 0; avl -= (vl)) {
        // 1. Query next vl
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma" : [vl] "=r"(vl) : [avl] "r"(avl));

        // 2. Load to calculate exp 
        auto curr = &(src0_ptr[index]); 
        __asm__ __volatile__("vle32.v v10, (%[i])" ::[i] "r"(curr));

        // 3. Load max into a vector 
        __asm__ __volatile__("vfmv.v.f v16, %[max]" :: [max] "f"(max)); 

        // 4. Subtract 
        __asm__ __volatile__("vfsub.vv v10, v10, v16");

        // 5. Compute exp 
        // Declare constants
        float mul1 = 0.25;
        float mul2 = 0.333;
        float mul3 = 0.5;
        float one  = 1.0;

        // Perform exp
        __asm__ __volatile__("vfmul.vf v12, v10, %[scale]" ::[scale] "f"(mul1));    // y = x * 0.25
        __asm__ __volatile__("vfadd.vf v12, v12, %[scale]" ::[scale] "f"(one));     // y = 1 + y

        __asm__ __volatile__("vfmul.vv v12, v10, v12");                             // y = y * x
        __asm__ __volatile__("vfmul.vf v12, v12, %[scale]" ::[scale] "f"(mul2));    // y = y * 0.33
        __asm__ __volatile__("vfadd.vf v12, v12, %[scale]" ::[scale] "f"(one));     // y = 1 + y

        __asm__ __volatile__("vfmul.vv v12, v10, v12");                             // y = y * x
        __asm__ __volatile__("vfmul.vf v12, v12, %[scale]" ::[scale] "f"(mul3));    // y = y * 0.5
        __asm__ __volatile__("vfadd.vf v12, v12, %[scale]" ::[scale] "f"(one));     // y = 1 + y

        __asm__ __volatile__("vfmul.vv v12, v10, v12");                             // y = y * x
        __asm__ __volatile__("vfadd.vf v12, v12, %[scale]" ::[scale] "f"(one));     // y = 1 + y

        // 6. Store into src0_ptr
        __asm__ __volatile__("vse32.v v12, (%[o])" :: [o] "r"(curr));

        // 7. Reduce to get sum 
        float zero_val = 0.0f;
        __asm__ __volatile__("vfmv.v.f v8, %[z]" :: [z] "f"(zero_val));

        __asm__ __volatile__("vfredsum.vs v8, v12, v8");

        // 8. Increment the sum 
        TYPE temp_sum;
        __asm__ __volatile__("vmv.x.s %[o], v8" : [o] "=r"(temp_sum));
        sum += temp_sum;

        // 9. Next index
        index += vl;
    }


    // 3. Normalize  
    index = offset;
    for(auto avl = num_cols; avl > 0; avl -= (vl)) {
        // 1. Query next vl
        __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma" : [vl] "=r"(vl) : [avl] "r"(avl));
        
        // 2. Load to calculate the dst_ptr  
        auto curr = &(src0_ptr[index]); 
        __asm__ __volatile__("vle32.v v10, (%[i])" ::[i] "r"(curr));

        // 3. Load sum into a vector register 
        __asm__ __volatile__("vfmv.v.f v16, %[s]" :: [s] "f"(sum));

        // 4. Divide 
        __asm__ __volatile__("vfdiv.vv v24, v10, v16");

        // 5. Store back into dst_ptr  
        auto c = &(dst_ptr[index]);
        __asm__ __volatile__("vse32.v v24, (%[o])" ::[o] "r"(c));

        // 6. Next index 
        index += vl; 
    }

}

int main() {
	kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
	return vx_spawn_threads(1, &arg->num_rows, nullptr, (vx_kernel_func_cb)kernel_body, arg);
}

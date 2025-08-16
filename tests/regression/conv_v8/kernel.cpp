#include <vx_spawn.h>
#include "common.h"

void kernel_body(kernel_arg_t* __UNIFORM__ arg) {
    auto I = reinterpret_cast<TYPE*>(arg->I_addr);
    auto W = reinterpret_cast<TYPE*>(arg->use_lmem ? __local_mem(0) : (void*)arg->W_addr);
	auto O = reinterpret_cast<TYPE*>(arg->O_addr);
    auto width = arg->width;

    int col = blockIdx.x;
    int row = blockIdx.y;


    // Generalize kernel size
    const int K = 7;          // 7x7 kernel
    const int pad = 3;    // = 3

    // Adjust for padded borders
    int paddedWidth = width + 2 * pad;
    int paddedX = col + pad;
    int paddedY = row + pad;

    // Compute 7x7 convolution sum
    uint32_t vl;
    float sum = 0.0f;

    // Row 0
    // 0. Query
    __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma" : [vl] "=r"(vl) : [avl] "r"(7));

    // 1. Load Input 
    auto a0 = &(I[(paddedY-3) * paddedWidth + (paddedX-3)]);
    __asm__ __volatile__("vle32.v v1, (%[i])" ::[i] "r"(a0));

    // 2. Load Weight
    auto b0 = &(W[0]);
    __asm__ __volatile__("vle32.v v2, (%[i])" ::[i] "r"(b0));

    // 3. FMacc
    __asm__ __volatile__("vmv.v.i v3, 0"); 
    __asm__ __volatile__("vfmacc.vv v3, v1, v2");

    // 4. Reduction 
    float zero_val = 0.0f;
    __asm__ __volatile__("vfmv.v.f v4, %[z]" :: [z] "f"(zero_val));
    __asm__ __volatile__("vfredsum.vs v4, v3, v4");
        
    // 5 Do the sum
    TYPE temp_sum;
    __asm__ __volatile__("vmv.x.s %[o], v4" : [o] "=r"(temp_sum));
    sum += temp_sum;



    // Row 1
    // 1. Load Input 
    auto a1 = &(I[(paddedY-2) * paddedWidth + (paddedX-3)]);
    __asm__ __volatile__("vle32.v v5, (%[i])" ::[i] "r"(a1));

    // 2. Load Weight
    auto b1 = &(W[7]);
    __asm__ __volatile__("vle32.v v6, (%[i])" ::[i] "r"(b1));

    // 3. FMacc
    __asm__ __volatile__("vmv.v.i v7, 0"); 
    __asm__ __volatile__("vfmacc.vv v7, v5, v6");

    // 4. Reduction 
    float zero_val1 = 0.0f;
    __asm__ __volatile__("vfmv.v.f v8, %[z]" :: [z] "f"(zero_val1));
    __asm__ __volatile__("vfredsum.vs v8, v7, v8");
        
    // 5 Do the sum
    TYPE temp_sum1;
    __asm__ __volatile__("vmv.x.s %[o], v8" : [o] "=r"(temp_sum1));
    sum += temp_sum1;

    
    // Row 2
    // Row 1
    // 1. Load Input 
    auto a2 = &(I[(paddedY-1) * paddedWidth + (paddedX-3)]);
    __asm__ __volatile__("vle32.v v9, (%[i])" ::[i] "r"(a2));

    // 2. Load Weight
    auto b2 = &(W[14]);
    __asm__ __volatile__("vle32.v v10, (%[i])" ::[i] "r"(b2));

    // 3. FMacc
    __asm__ __volatile__("vmv.v.i v11, 0"); 
    __asm__ __volatile__("vfmacc.vv v11, v9, v10");

    // 4. Reduction 
    float zero_val2 = 0.0f;
    __asm__ __volatile__("vfmv.v.f v12, %[z]" :: [z] "f"(zero_val2));
    __asm__ __volatile__("vfredsum.vs v12, v11, v12");

    // 5 Do the sum
    TYPE temp_sum2;
    __asm__ __volatile__("vmv.x.s %[o], v12" : [o] "=r"(temp_sum2));
    sum += temp_sum2;


    // Row 3
    auto a2 = &(I[(paddedY-1) * paddedWidth + (paddedX-3)]);
    __asm__ __volatile__("vle32.v v9, (%[i])" ::[i] "r"(a2));

    // 2. Load Weight
    auto b2 = &(W[14]);
    __asm__ __volatile__("vle32.v v10, (%[i])" ::[i] "r"(b2));

    // 3. FMacc
    __asm__ __volatile__("vmv.v.i v11, 0"); 
    __asm__ __volatile__("vfmacc.vv v11, v9, v10");

    // 4. Reduction 
    float zero_val2 = 0.0f;
    __asm__ __volatile__("vfmv.v.f v12, %[z]" :: [z] "f"(zero_val2));
    __asm__ __volatile__("vfredsum.vs v12, v11, v12");

    // 5 Do the sum
    TYPE temp_sum2;
    __asm__ __volatile__("vmv.x.s %[o], v12" : [o] "=r"(temp_sum2));
    sum += temp_sum2;


    sum += I[(paddedY+0) * paddedWidth + (paddedX-3)] * W[21];
    sum += I[(paddedY+0) * paddedWidth + (paddedX-2)] * W[22];
    sum += I[(paddedY+0) * paddedWidth + (paddedX-1)] * W[23];
    sum += I[(paddedY+0) * paddedWidth + (paddedX+0)] * W[24];
    sum += I[(paddedY+0) * paddedWidth + (paddedX+1)] * W[25];
    sum += I[(paddedY+0) * paddedWidth + (paddedX+2)] * W[26];
    sum += I[(paddedY+0) * paddedWidth + (paddedX+3)] * W[27];
    
    // Row 4
    sum += I[(paddedY+1) * paddedWidth + (paddedX-3)] * W[28];
    sum += I[(paddedY+1) * paddedWidth + (paddedX-2)] * W[29];
    sum += I[(paddedY+1) * paddedWidth + (paddedX-1)] * W[30];
    sum += I[(paddedY+1) * paddedWidth + (paddedX+0)] * W[31];
    sum += I[(paddedY+1) * paddedWidth + (paddedX+1)] * W[32];
    sum += I[(paddedY+1) * paddedWidth + (paddedX+2)] * W[33];
    sum += I[(paddedY+1) * paddedWidth + (paddedX+3)] * W[34];
    
    // Row 5
    sum += I[(paddedY+2) * paddedWidth + (paddedX-3)] * W[35];
    sum += I[(paddedY+2) * paddedWidth + (paddedX-2)] * W[36];
    sum += I[(paddedY+2) * paddedWidth + (paddedX-1)] * W[37];
    sum += I[(paddedY+2) * paddedWidth + (paddedX+0)] * W[38];
    sum += I[(paddedY+2) * paddedWidth + (paddedX+1)] * W[39];
    sum += I[(paddedY+2) * paddedWidth + (paddedX+2)] * W[40];
    sum += I[(paddedY+2) * paddedWidth + (paddedX+3)] * W[41];
    
    // Row 6
    sum += I[(paddedY+3) * paddedWidth + (paddedX-3)] * W[42];
    sum += I[(paddedY+3) * paddedWidth + (paddedX-2)] * W[43];
    sum += I[(paddedY+3) * paddedWidth + (paddedX-1)] * W[44];
    sum += I[(paddedY+3) * paddedWidth + (paddedX+0)] * W[45];
    sum += I[(paddedY+3) * paddedWidth + (paddedX+1)] * W[46];
    sum += I[(paddedY+3) * paddedWidth + (paddedX+2)] * W[47];
    sum += I[(paddedY+3) * paddedWidth + (paddedX+3)] * W[48];


    O[row * width + col] = sum;
}

int main() {
    kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);
    if (arg->use_lmem) {
        // populate local memory
        auto W = reinterpret_cast<TYPE*>(arg->W_addr);
        auto L = reinterpret_cast<TYPE*>(__local_mem(0));
        for (int i = 0; i < (7*7); ++i) {
            L[i] = W[i];
        }
    }
    return vx_spawn_threads(2, arg->grid_dim, nullptr, (vx_kernel_func_cb)kernel_body, arg);
}

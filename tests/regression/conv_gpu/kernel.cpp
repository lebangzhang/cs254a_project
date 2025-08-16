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
    float sum = 0.0f;

    // Option 1 
    for (int ky = -pad; ky <= pad; ky++) {
        for (int kx = -pad; kx <= pad; kx++) {
            int iy = paddedY + ky;
            int ix = paddedX + kx;
            float value  = I[iy * paddedWidth + ix];
            float weight = W[(ky + pad) * K + (kx + pad)];
            sum += value * weight;
        }
    }

    // Option 2 (Slower) 
    /*for (int i = 0; i < K * K; i++) {*/
        /*int ky = i / K - pad;      // row offset*/
        /*int kx = i % K - pad;      // column offset*/
        /*int iy = paddedY + ky;*/
        /*int ix = paddedX + kx;*/
        /*float value  = I[iy * paddedWidth + ix];*/
        /*float weight = W[i];*/
        /*sum += value * weight;*/
    /*}*/


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

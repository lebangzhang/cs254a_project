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


    float sum = 0.0f;

// Row 0
sum += I[(paddedY-3) * paddedWidth + (paddedX-3)] * W[0*7 + 0];
sum += I[(paddedY-3) * paddedWidth + (paddedX-2)] * W[0*7 + 1];
sum += I[(paddedY-3) * paddedWidth + (paddedX-1)] * W[0*7 + 2];
sum += I[(paddedY-3) * paddedWidth + (paddedX+0)] * W[0*7 + 3];
sum += I[(paddedY-3) * paddedWidth + (paddedX+1)] * W[0*7 + 4];
sum += I[(paddedY-3) * paddedWidth + (paddedX+2)] * W[0*7 + 5];
sum += I[(paddedY-3) * paddedWidth + (paddedX+3)] * W[0*7 + 6];

// Row 1
sum += I[(paddedY-2) * paddedWidth + (paddedX-3)] * W[1*7 + 0];
sum += I[(paddedY-2) * paddedWidth + (paddedX-2)] * W[1*7 + 1];
sum += I[(paddedY-2) * paddedWidth + (paddedX-1)] * W[1*7 + 2];
sum += I[(paddedY-2) * paddedWidth + (paddedX+0)] * W[1*7 + 3];
sum += I[(paddedY-2) * paddedWidth + (paddedX+1)] * W[1*7 + 4];
sum += I[(paddedY-2) * paddedWidth + (paddedX+2)] * W[1*7 + 5];
sum += I[(paddedY-2) * paddedWidth + (paddedX+3)] * W[1*7 + 6];

// Row 2
sum += I[(paddedY-1) * paddedWidth + (paddedX-3)] * W[2*7 + 0];
sum += I[(paddedY-1) * paddedWidth + (paddedX-2)] * W[2*7 + 1];
sum += I[(paddedY-1) * paddedWidth + (paddedX-1)] * W[2*7 + 2];
sum += I[(paddedY-1) * paddedWidth + (paddedX+0)] * W[2*7 + 3];
sum += I[(paddedY-1) * paddedWidth + (paddedX+1)] * W[2*7 + 4];
sum += I[(paddedY-1) * paddedWidth + (paddedX+2)] * W[2*7 + 5];
sum += I[(paddedY-1) * paddedWidth + (paddedX+3)] * W[2*7 + 6];

// Row 3
sum += I[(paddedY+0) * paddedWidth + (paddedX-3)] * W[3*7 + 0];
sum += I[(paddedY+0) * paddedWidth + (paddedX-2)] * W[3*7 + 1];
sum += I[(paddedY+0) * paddedWidth + (paddedX-1)] * W[3*7 + 2];
sum += I[(paddedY+0) * paddedWidth + (paddedX+0)] * W[3*7 + 3];
sum += I[(paddedY+0) * paddedWidth + (paddedX+1)] * W[3*7 + 4];
sum += I[(paddedY+0) * paddedWidth + (paddedX+2)] * W[3*7 + 5];
sum += I[(paddedY+0) * paddedWidth + (paddedX+3)] * W[3*7 + 6];

// Row 4
sum += I[(paddedY+1) * paddedWidth + (paddedX-3)] * W[4*7 + 0];
sum += I[(paddedY+1) * paddedWidth + (paddedX-2)] * W[4*7 + 1];
sum += I[(paddedY+1) * paddedWidth + (paddedX-1)] * W[4*7 + 2];
sum += I[(paddedY+1) * paddedWidth + (paddedX+0)] * W[4*7 + 3];
sum += I[(paddedY+1) * paddedWidth + (paddedX+1)] * W[4*7 + 4];
sum += I[(paddedY+1) * paddedWidth + (paddedX+2)] * W[4*7 + 5];
sum += I[(paddedY+1) * paddedWidth + (paddedX+3)] * W[4*7 + 6];

// Row 5
sum += I[(paddedY+2) * paddedWidth + (paddedX-3)] * W[5*7 + 0];
sum += I[(paddedY+2) * paddedWidth + (paddedX-2)] * W[5*7 + 1];
sum += I[(paddedY+2) * paddedWidth + (paddedX-1)] * W[5*7 + 2];
sum += I[(paddedY+2) * paddedWidth + (paddedX+0)] * W[5*7 + 3];
sum += I[(paddedY+2) * paddedWidth + (paddedX+1)] * W[5*7 + 4];
sum += I[(paddedY+2) * paddedWidth + (paddedX+2)] * W[5*7 + 5];
sum += I[(paddedY+2) * paddedWidth + (paddedX+3)] * W[5*7 + 6];

// Row 6
sum += I[(paddedY+3) * paddedWidth + (paddedX-3)] * W[6*7 + 0];
sum += I[(paddedY+3) * paddedWidth + (paddedX-2)] * W[6*7 + 1];
sum += I[(paddedY+3) * paddedWidth + (paddedX-1)] * W[6*7 + 2];
sum += I[(paddedY+3) * paddedWidth + (paddedX+0)] * W[6*7 + 3];
sum += I[(paddedY+3) * paddedWidth + (paddedX+1)] * W[6*7 + 4];
sum += I[(paddedY+3) * paddedWidth + (paddedX+2)] * W[6*7 + 5];
sum += I[(paddedY+3) * paddedWidth + (paddedX+3)] * W[6*7 + 6];



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

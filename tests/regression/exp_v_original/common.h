
#ifndef _COMMON_H_
#define _COMMON_H_

#ifndef TYPE
#define TYPE float
#endif

typedef struct {
  uint32_t num_points;

  uint32_t vec_len_per_thread;
  uint32_t num_threads_to_run; 

  uint64_t src0_addr;
  uint64_t dst_addr; 
} kernel_arg_t;

inline float exp(float x){
    float y = 1 + x * 0.25;
    y = 1 + x * y * 0.333;
    y = 1 + x * y * 0.5;
    y = 1 + x * y;
    return y;
}

inline void exp_rv_float32(float *a, float *d){
    
    // Load Input 
    __asm__ __volatile__("vle32.v v10, (%[i])" ::[i] "r"(a));

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
        
    // Store to dest 
    __asm__ __volatile__("vse32.v v12, (%[o])" ::[o] "r"(d));
}

#endif

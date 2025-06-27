#ifndef _COMMON_H_
#define _COMMON_H_

#ifndef TYPE
/*#define TYPE float*/
#define TYPE int
#endif

typedef struct {
  uint32_t num_points;

  uint32_t vec_len_per_thread;
  uint32_t num_threads_to_run;

  uint64_t src0_addr;
  uint64_t src1_addr;
  uint64_t dst_addr;  
} kernel_arg_t;

#endif

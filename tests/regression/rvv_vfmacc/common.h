#ifndef _COMMON_H_
#define _COMMON_H_

typedef struct {
  uint32_t f32_size;
  uint32_t f64_size;
  uint64_t f32_acc_addr;
  uint64_t f32_lhs_addr;
  uint64_t f32_rhs_addr;
  uint64_t f32_dst_addr;
  uint64_t f64_acc_addr;
  uint64_t f64_lhs_addr;
  uint64_t f64_rhs_addr;
  uint64_t f64_dst_addr;
} kernel_arg_t;

#endif

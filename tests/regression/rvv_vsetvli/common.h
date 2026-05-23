#ifndef _COMMON_H_
#define _COMMON_H_

typedef struct {
  uint32_t avl;
  uint32_t vtype;
  uint32_t expected_vsetvli;
  uint32_t expected_vsetivli;
  uint32_t expected_vsetvl;
  uint64_t out_addr;
} kernel_arg_t;

#endif

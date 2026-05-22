#ifndef _COMMON_H_
#define _COMMON_H_

typedef struct {
  uint32_t data_size;
  uint32_t index_count;
  uint64_t src_addr;
  uint64_t dst_addr;
  uint64_t idx_addr;
} kernel_arg_t;

#endif

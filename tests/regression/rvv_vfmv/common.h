#ifndef _COMMON_H_
#define _COMMON_H_

typedef struct {
  uint32_t size;
  uint32_t vlen;
  uint64_t scalar_addr;
  uint64_t dst_addr;
} kernel_arg_t;

#endif

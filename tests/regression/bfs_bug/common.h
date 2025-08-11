#ifndef _COMMON_H_
#define _COMMON_H_

#ifndef TYPE
/*#define TYPE float*/
#define TYPE int
#endif


struct Node {
    int starting;
    int no_of_edges;
};


typedef struct {
  uint32_t num_nodes;
  uint32_t num_edges;

  uint64_t src0_addr;  // Node        Buffer
  uint64_t src1_addr;  // Edge        Buffer 
  uint64_t src2_addr;  // Mask        Buffer 
  uint64_t src3_addr;  // Update Mask Buffer 
  /*uint64_t src4_addr;  // Visited     Buffer */
  uint64_t dst_addr;   // Cost Buffer
} kernel_arg_t;


#endif

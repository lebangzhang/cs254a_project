#ifndef _COMMON_H_
#define _COMMON_H_

#ifndef TYPE
/*#define TYPE float*/
#define TYPE int
#endif


struct Node {
    uint32_t starting;
    uint32_t no_of_edges;
};


typedef struct {
  uint32_t num_nodes;
  uint32_t num_edges;
  uint32_t vec_len_per_thread;
  uint32_t num_threads_to_run;

  uint64_t src0_addr;  // Node          Buffer
  uint64_t src1_addr;  // Edge          Buffer 
  uint64_t src2_addr;  // Mask          Buffer 
  uint64_t src3_addr;  // Update Mask   Buffer 
  uint64_t src4_addr;  // Visited       Buffer 
  uint64_t src5_addr;  // Thread Update Buffer 
  uint64_t dst_addr;   // Cost Buffer
} kernel_arg_t;



#define MAX_NODES 100000
void bfs_cpu(Node graph_nodes[], uint32_t graph_edges[], int num_nodes, int source, uint32_t cost[]) {
    int queue[MAX_NODES];
    int head = 0, tail = 0;

    // Initialize cost array to -1 (unvisited)
    for (int i = 0; i < num_nodes; i++) {
        cost[i] = -1;
    }

    // Start node cost = 0 and enqueue it
    cost[source] = 0;
    queue[tail++] = source;

    while (head < tail) {
        int current = queue[head++];
        Node node = graph_nodes[current];
        int start = node.starting;
        int end = start + node.no_of_edges;

        for (int i = start; i < end; i++) {
            int neighbor = graph_edges[i];
            if (cost[neighbor] == -1) {  // Not visited yet
                cost[neighbor] = cost[current] + 1;
                queue[tail++] = neighbor;
            }
        }
    }
}



#endif

#include <vx_spawn.h>
#include "common.h"
#include "vx_print.h"


void kernel_body(kernel_arg_t* __UNIFORM__ arg) {
	auto node          = reinterpret_cast<TYPE*>(arg->src0_addr);
	auto edge          = reinterpret_cast<TYPE*>(arg->src1_addr);
	auto mask          = reinterpret_cast<TYPE*>(arg->src2_addr);
	auto update        = reinterpret_cast<TYPE*>(arg->src3_addr);
	auto visit         = reinterpret_cast<TYPE*>(arg->src4_addr);
    auto thread_update = reinterpret_cast<TYPE*>(arg->src5_addr);
	auto cost          = reinterpret_cast<TYPE*>(arg->dst_addr);
    auto num_nodes = arg->num_nodes;

    uint32_t tid   = blockIdx.x;
    uint32_t index = tid * 2;       // x2 because each Node has 2 elements
   
    /*dst[tid] = node[index]; */

    if ( (tid < num_nodes) && (mask[tid])) {
        
        mask[tid] = 0;

        int start = node[index];
        int edges = node[index + 1];
        int end = start + edges; 

        /*vx_printf("Start: %d\n", start);*/
        /*vx_printf("End  : %d\n", end);*/

        for (int i = start; i < end; i++) {

            int nid     = edge[i];
            int visited = visit[nid];
            /*vx_printf("NID: %d\n", nid);*/

            if (!visited){
                /*vx_printf("NID: %d\n", nid);*/
                cost[nid] = cost[tid] + 1;
                update[nid] = 1;
                thread_update[tid] = 1;    // Indicate thread did something
            }
        }
    }
    __syncthreads();
}

void update_frontier(kernel_arg_t* __UNIFORM__ arg) {
	auto mask          = reinterpret_cast<TYPE*>(arg->src2_addr);
	auto update        = reinterpret_cast<TYPE*>(arg->src3_addr);
	auto visit         = reinterpret_cast<TYPE*>(arg->src4_addr);
    auto thread_update = reinterpret_cast<TYPE*>(arg->src5_addr);
    auto num_nodes = arg->num_nodes;

    uint32_t tid   = blockIdx.x;

    if ((tid < num_nodes) and (update[tid]) ) {
        mask[tid] = 1;   // move to next frontier
        visit[tid] = 1;
        update[tid] = 0;
    }

    // Reset workload
    thread_update[tid] = 0;

    __syncthreads();
}


int main() {
	kernel_arg_t* arg = (kernel_arg_t*)csr_read(VX_CSR_MSCRATCH);

	auto mask          = reinterpret_cast<TYPE*>(arg->src2_addr);
	auto update        = reinterpret_cast<TYPE*>(arg->src3_addr);
	auto visit         = reinterpret_cast<TYPE*>(arg->src4_addr);
    auto thread_update = reinterpret_cast<TYPE*>(arg->src5_addr);
    auto num_nodes = arg->num_nodes;

    int continue_flag = 0;

    do{
        continue_flag = 0;

        // 1. Current iteration 
        vx_spawn_threads(1, &arg->num_nodes, nullptr, (vx_kernel_func_cb)kernel_body, arg);

        // 2. Check to stop 
        for(int i=0; i < num_nodes; i++){
            if(thread_update[i]){
                continue_flag = 1;
                break;
            }
        }

        // 3. Prepare next iteration
        
        // Original Code
        /*vx_spawn_threads(1, &arg->num_nodes, nullptr, (vx_kernel_func_cb)update_frontier, arg);*/

        // For Vector
        // Moved update_frontier into current main loop because tid isn't working for other loop
        for(uint32_t tid = 0; tid < arg->num_nodes; tid++){
            if ((tid < num_nodes) and (update[tid]) ) {
                mask[tid] = 1;   // move to next frontier
                visit[tid] = 1;
                update[tid] = 0;
            }
            // Reset workload
            thread_update[tid] = 0;
        }


    } while(continue_flag);
}


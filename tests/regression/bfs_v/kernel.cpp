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
    
    if ( (tid < num_nodes) && (mask[tid])) {
        
        mask[tid] = 0;

        int start = node[index];
        int edges = node[index + 1];
        int end = start + edges; 

        /*vx_printf("Start: %d\n", start);*/
        /*vx_printf("End  : %d\n", end);*/

        // Vectorized inner loop 
        uint32_t vl;
        uint32_t edge_index = start;

        for(auto avl = edges; avl > 0; avl -= (vl)) {
            // 1. Query next vl
            __asm__ __volatile__("vsetvli %[vl], %[avl], e32, m1, ta, ma" : [vl] "=r"(vl) : [avl] "r"(avl));
    
            // 2. Load node_id (nid) from edges  --> Store in v10
            auto a = &(edge[edge_index]);
            __asm__ __volatile__("vle32.v v10, (%[i])" ::[i] "r"(a));

            // Debug Content  
            /*int A[1000];*/
            /*__asm__ __volatile__("vse32.v v10, (%[o])" ::[o] "r"(&A));*/
            /*vx_printf("A[0]=%d\n", A[0]);*/
            /*vx_printf("A[1]=%d\n", A[1]);*/
            /*vx_printf("A[2]=%d\n", A[2]);*/
    
            //3. Gather load visit data from edge 
            // Data stored in v12 
            // Index offset x4 in v10
            auto visited = &(visit[0]);
            __asm__ __volatile__("li t0, 4");
            __asm__ __volatile__("vmul.vx v10, v10, t0");     // For some weird reason, need to apply x4 offset for XLEN=32
            __asm__ __volatile__("vluxei32.v v12, (%[base]), v10" :: [base] "r"(visited));
    
            // 4. Create mask for visited         --> Store in v0
            __asm__ __volatile__("vmseq.vi v0, v12, 0x0");
    
            // 5. Create the cost[tid] + 1 
            uint32_t cost_tid_plus_1 = cost[tid] + 1;
            __asm__ __volatile__("vmv.v.x v2, %0" : : "r"(cost_tid_plus_1));
    
            // 6. Scatter store cost[nid] = cost_tid_plus_1 
            auto cost_ptr = &(cost[0]);
            __asm__ __volatile__ ("vsuxei32.v v2, (%0), v10, v0.t" :: "r"(cost_ptr));
    
            // 7. Scatter store update[nid] = 1  
            auto update_ptr = &(update[0]);
            __asm__ __volatile__("vmv.v.i v4, 1");
            __asm__ __volatile__ ("vsuxei32.v v4, (%0), v10, v0.t" :: "r"(update_ptr));
    
            // 8. Apply thread_update[tid]
            __asm__ __volatile__("vmv.s.x v4, zero");
            __asm__ __volatile__("vredsum.vs v4, v0, v4");
            int sum_result;
            __asm__ __volatile__("vse32.v v4, (%[o])" ::[o] "r"(&sum_result));
    
            // Note:  Because v0 is in bitwise format and not elementwise format due to masking 
            // Hence: sum_result is element wise and not bitwise, hence, we just check if non 0
            /*vx_printf("Sum_result=%d, tid=%d\n", sum_result, tid);*/
            thread_update[tid] = (sum_result != 0) ? 1 : 0;
        
            edge_index += vl;
        }
    }
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

    /*vx_printf("TID=%d\n",tid);*/
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
            if (update[tid]) {
                mask[tid] = 1;   // move to next frontier
                visit[tid] = 1;
                update[tid] = 0;
            }
            // Reset workload
            thread_update[tid] = 0;
        }

        /*vx_printf("-------------\n");*/

        for(uint32_t tid = 0; tid < arg->num_nodes; tid++){
            if (mask[tid]) {
                /*vx_printf("%d\n", tid);*/
            }
        }
        /*vx_printf("-------------\n");*/



    } while(continue_flag);

    /*vx_spawn_threads(1, &arg->num_nodes, nullptr, (vx_kernel_func_cb)kernel_body, arg);*/
    /*vx_spawn_threads(1, &arg->num_nodes, nullptr, (vx_kernel_func_cb)update_frontier, arg);*/
    /*vx_spawn_threads(1, &arg->num_nodes, nullptr, (vx_kernel_func_cb)kernel_body, arg);*/
    /*vx_spawn_threads(1, &arg->num_nodes, nullptr, (vx_kernel_func_cb)update_frontier, arg);*/
    /*vx_spawn_threads(1, &arg->num_nodes, nullptr, (vx_kernel_func_cb)kernel_body, arg);*/
    /*vx_spawn_threads(1, &arg->num_nodes, nullptr, (vx_kernel_func_cb)update_frontier, arg);*/
    /*vx_spawn_threads(1, &arg->num_nodes, nullptr, (vx_kernel_func_cb)kernel_body, arg);*/
    /*vx_spawn_threads(1, &arg->num_nodes, nullptr, (vx_kernel_func_cb)update_frontier, arg);*/
}


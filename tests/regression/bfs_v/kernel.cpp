#include <vx_spawn2.h>
#include "common.h"

extern "C" void kernel_main(kernel_arg_t* __UNIFORM__ arg) {
    uint32_t tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid >= arg->frontier_size)
        return;

    auto *__restrict nodes    = reinterpret_cast<Node *>(arg->nodes_addr);
    auto *__restrict edges    = reinterpret_cast<int32_t *>(arg->edges_addr);
    auto *__restrict visit    = reinterpret_cast<uint8_t *>(arg->visit_addr);
    auto *__restrict nextmask = reinterpret_cast<uint8_t *>(arg->nextmask_addr);
    auto *__restrict frontier = reinterpret_cast<uint32_t *>(arg->frontier_addr);
    auto *__restrict cost     = reinterpret_cast<int32_t *>(arg->cost_addr);

    uint32_t v     = frontier[tid];
    uint32_t start = nodes[v].starting;
    uint32_t end   = start + nodes[v].no_of_edges;
    int32_t  cv    = cost[v] + 1;

    uint32_t i = start;
    while (i < end) {
        uint32_t remaining = end - i;
        uint32_t vl;

        // 1. Set vector length
        __asm__ __volatile__("vsetvli %[vl], %[rem], e32, m4, ta, ma"
                        : [vl] "=r"(vl) : [rem] "r"(remaining));

        // 2. Load edge indices into v4
        auto ep = &edges[i];
        __asm__ __volatile__("vle32.v v4, (%[i])" : : [i] "r"(ep) : "memory");

        // 3. Gather visit status: v8[j] = visit[edges[i+j]]
        __asm__ __volatile__("vluxei32.v v8, (%[b]), v4" : : [b] "r"(visit) : "memory");

        // 4. Mask: v0 = (v8 == 0) — unvisited neighbours
        __asm__ __volatile__("vmseq.vi v0, v8, 0");

        // 5. Scatter nextmask[nid] = 1 for unvisited
        __asm__ __volatile__("vmv.v.i v12, 1");
        __asm__ __volatile__("vsuxei32.v v12, (%[b]), v4, v0.t" : : [b] "r"(nextmask) : "memory");

        // 6. Scatter cost[nid] = cv for unvisited
        __asm__ __volatile__("vmv.v.x v16, %[cv]" : : [cv] "r"(cv));
        __asm__ __volatile__("vsuxei32.v v16, (%[b]), v4, v0.t" : : [b] "r"(cost) : "memory");

        i += vl;
    }
}

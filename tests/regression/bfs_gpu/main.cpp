#include <iostream>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <vortex.h>
#include "common.h"

#define FLOAT_ULP 6

#define RT_CHECK(_expr)                                         \
   do {                                                         \
     int _ret = _expr;                                          \
     if (0 == _ret)                                             \
       break;                                                   \
     printf("Error: '%s' returned %d!\n", #_expr, (int)_ret);   \
	 cleanup();			                                              \
     exit(-1);                                                  \
   } while (false)

///////////////////////////////////////////////////////////////////////////////

template <typename Type>
class Comparator {};

template <>
class Comparator<int> {
public:
  static const char* type_str() {
    return "integer";
  }
  static int generate() {
    return rand() % 100;
  }
  static bool compare(int a, int b, int index, int errors) {
    if (a != b) {
      if (errors < 100) {
        printf("*** error: [%d] expected=%d, actual=%d\n", index, b, a);
      }
      return false;
    }
    return true;
  }
};

template <>
class Comparator<float> {
private:
  union Float_t { float f; int i; };
public:
  static const char* type_str() {
    return "float";
  }
  static float generate() {
    return static_cast<float>(rand()) / RAND_MAX;
  }
  static bool compare(float a, float b, int index, int errors) {
    union fi_t { float f; int32_t i; };
    fi_t fa, fb;
    fa.f = a;
    fb.f = b;
    auto d = std::abs(fa.i - fb.i);
    if (d > FLOAT_ULP) {
      if (errors < 100) {
        printf("*** error: [%d] expected=%f, actual=%f\n", index, b, a);
      }
      return false;
    }
    return true;
  }
};

void generate_random_graph(int num_nodes, int max_edges_per_node, 
                           std::vector<Node>& nodes, std::vector<int>& edges) {
    nodes.resize(num_nodes);
    edges.clear();

    int edge_count = 0;
    for (int i = 0; i < num_nodes; ++i) {
        nodes[i].starting = edge_count;
        int num_edges = rand() % (max_edges_per_node + 1);
        nodes[i].no_of_edges = num_edges;

        for (int e = 0; e < num_edges; ++e) {
            int dest_node = Comparator<TYPE>::generate() % num_nodes;
            edges.push_back(dest_node);
            edge_count++;
        }
    }
}




const char* kernel_file = "kernel.vxbin";
uint32_t size = 1024;

vx_device_h device = nullptr;
vx_buffer_h src0_buffer = nullptr;
vx_buffer_h src1_buffer = nullptr;
vx_buffer_h src2_buffer = nullptr;
vx_buffer_h src3_buffer = nullptr;
vx_buffer_h src4_buffer = nullptr;
vx_buffer_h src5_buffer = nullptr;
vx_buffer_h dst_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

static void show_usage() {
   std::cout << "Vortex Test." << std::endl;
   std::cout << "Usage: [-k: kernel] [-n words] [-h: help]" << std::endl;
}

static void parse_args(int argc, char **argv) {
  int c;
  while ((c = getopt(argc, argv, "n:k:h")) != -1) {
    switch (c) {
    case 'n':
      size = atoi(optarg);
      break;
    case 'k':
      kernel_file = optarg;
      break;
    case 'h':
      show_usage();
      exit(0);
      break;
    default:
      show_usage();
      exit(-1);
    }
  }
}

void cleanup() {
  if (device) {
    vx_mem_free(src0_buffer);
    vx_mem_free(src1_buffer);
    vx_mem_free(src2_buffer);
    vx_mem_free(src3_buffer);
    vx_mem_free(src4_buffer);
    vx_mem_free(src5_buffer);
    vx_mem_free(dst_buffer);
    vx_mem_free(krnl_buffer);
    vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

int main(int argc, char *argv[]) {
  // parse command arguments
  parse_args(argc, argv);

  std::srand(50);

  // open device connection
  std::cout << "open device connection" << std::endl;
  RT_CHECK(vx_dev_open(&device));

  // Generate graph 
  int max_edges_per_node = 5;
  std::vector<Node> h_nodes;
  std::vector<int> h_edges;
  generate_random_graph(size, max_edges_per_node, h_nodes, h_edges);

  /*std:cout << "Nodes:\n";*/
  /*for (int i = 0; i < size; ++i) {*/
  /*  std::cout << "Node " << i << ": starting=" << h_nodes[i].starting << ", no_of_edges=" << h_nodes[i].no_of_edges << "\n";*/
  /*}*/
  /**/
  /*std::cout << "Edges:\n";*/
  /*for (int e : h_edges) {*/
  /*  std::cout << e << " ";*/
  /*}*/
  /*std::cout << "\n";*/


  // Assignments
  uint32_t num_nodes = size; 
  uint32_t num_edges = h_edges.size();

  uint32_t nodes_buf_size = num_nodes  * sizeof(Node);
  uint32_t edges_buf_size = num_edges  * sizeof(int);  
  uint32_t cost_buf_size  = num_nodes  * sizeof(int);

  std::cout << "number of nodes: " << num_nodes << std::endl;

  kernel_arg.num_nodes  = num_nodes;
  kernel_arg.num_edges  = num_edges;


  // allocate device memory
  std::cout << "allocate device memory" << std::endl;
  RT_CHECK(vx_mem_alloc(device, nodes_buf_size, VX_MEM_READ_WRITE, &src0_buffer));
  RT_CHECK(vx_mem_address(src0_buffer, &kernel_arg.src0_addr));
  RT_CHECK(vx_mem_alloc(device, edges_buf_size, VX_MEM_READ_WRITE, &src1_buffer));
  RT_CHECK(vx_mem_address(src1_buffer, &kernel_arg.src1_addr));
  RT_CHECK(vx_mem_alloc(device, nodes_buf_size, VX_MEM_READ_WRITE, &src2_buffer));
  RT_CHECK(vx_mem_address(src2_buffer, &kernel_arg.src2_addr));
  RT_CHECK(vx_mem_alloc(device, nodes_buf_size, VX_MEM_READ_WRITE, &src3_buffer));
  RT_CHECK(vx_mem_address(src3_buffer, &kernel_arg.src3_addr));
  RT_CHECK(vx_mem_alloc(device, nodes_buf_size, VX_MEM_READ_WRITE, &src4_buffer));
  RT_CHECK(vx_mem_address(src4_buffer, &kernel_arg.src4_addr));
  RT_CHECK(vx_mem_alloc(device, nodes_buf_size, VX_MEM_READ_WRITE, &src5_buffer));
  RT_CHECK(vx_mem_address(src5_buffer, &kernel_arg.src5_addr));


  RT_CHECK(vx_mem_alloc(device, cost_buf_size, VX_MEM_READ_WRITE, &dst_buffer));
  RT_CHECK(vx_mem_address(dst_buffer, &kernel_arg.dst_addr));

  std::cout << "dev_src0=0x" << std::hex << kernel_arg.src0_addr << std::endl;
  std::cout << "dev_src1=0x" << std::hex << kernel_arg.src1_addr << std::endl;
  std::cout << "dev_src2=0x" << std::hex << kernel_arg.src2_addr << std::endl;
  std::cout << "dev_src3=0x" << std::hex << kernel_arg.src3_addr << std::endl;
  std::cout << "dev_src4=0x" << std::hex << kernel_arg.src4_addr << std::endl;
  std::cout << "dev_src5=0x" << std::hex << kernel_arg.src5_addr << std::endl;
  std::cout << "dev_dst=0x" << std::hex << kernel_arg.dst_addr << std::endl;

  // allocate host buffers
  std::cout << "allocate host buffers" << std::endl;
  std::vector<Node> h_src0(num_nodes);
  std::vector<int> h_src1(num_edges);
  std::vector<int> h_src2(num_nodes);
  std::vector<int> h_src3(num_nodes);
  std::vector<int> h_src4(num_nodes);
  std::vector<int> h_src5(num_nodes);
  std::vector<int> h_dst(num_nodes);

  /*for (uint32_t i = 0; i < num_points; ++i) {*/
    /*h_src0[i] = Comparator<TYPE>::generate();*/
  /*}*/

  // Store in CSR (Compressed Sparse Row) format
/*  Node h_nodes[8] = {*/
/*    {0, 2},  // Node 0 edges start at 0, 2 edges*/
/*    {2, 3},  // Node 1 edges start at 2, 2 edges*/
/*    {4, 1},  // Node 2 edges start at 4, 1 edge*/
/*    {5, 1},  // Node 3 edges start at 5, 1 edge*/
/*    {6, 1},  // Node 4 edges start at 6, 1 edge*/
/*    {7, 1},  // Node 5 edges start at 7, 1 edge*/
/*    {8, 0},  // Node 6 no edges*/
/*    {8, 0}   // Node 7 no edges*/
/*};*/

/*int h_edges[8] = {*/
  /*  1, 2,    // Node 0 → Node 1, Node 2*/
  /*  3, 4,    // Node 1 → Node 3, Node 4*/
  /*  4,       // Node 2 → Node 4*/
  /*  5,       // Node 3 → Node 5*/
  /*  6,       // Node 4 → Node 6*/
  /*  7        // Node 5 → Node 7*/
  /*  // Nodes 6 and 7 have no outgoing edges*/
  /*};*/

/**/
/*  Node h_nodes[8] = {*/
/*    {0, 3},  // Node 0 edges start at 0, 2 edges*/
/*    {3, 2},  // Node 1 edges start at 2, 2 edges*/
/*    {5, 3},  // Node 2 edges start at 4, 1 edge*/
/*    {8, 1},  // Node 3 edges start at 5, 1 edge*/
/*    {9, 1},  // Node 4 edges start at 6, 1 edge*/
/*    {10, 1},  // Node 5 edges start at 7, 1 edge*/
/*    {11, 0},  // Node 6 no edges*/
/*    {11, 1}   // Node 7 no edges*/
/*};*/
/**/
/**/
/*  int h_edges[12] = { 2,7,2,5,1,4,4,6,4,1,5, 6};*/

  // Node 
  for(uint32_t i = 0; i < num_nodes; i++){
    h_src0[i] = h_nodes[i];
  }

  // Edge 
  for(uint32_t i = 0; i < num_edges; i++){
    h_src1[i] = h_edges[i];
  }

  // Masks 
  for(uint32_t i = 0; i < num_nodes; i++){
    h_src2[i] = 0; 
  }
  h_src2[0] = 1;

  // Visited
  for(uint32_t i = 0; i < num_nodes; i++){
    h_src4[i] = 0; 
  }
  h_src4[0] = 1;

  // Thread updated 
  for(uint32_t i = 0; i < num_nodes; i++){
    h_src5[i] = 0; 
  }

  // Cost 
  for(uint32_t i = 0; i < num_nodes; i++){
    h_dst[i] = -1; 
  }
  h_dst[0] = 0;

  // upload source buffer0
  std::cout << "upload source buffer0" << std::endl;
  RT_CHECK(vx_copy_to_dev(src0_buffer, h_src0.data(), 0, nodes_buf_size));

  // upload source buffer1
  std::cout << "upload source buffer1" << std::endl;
  RT_CHECK(vx_copy_to_dev(src1_buffer, h_src1.data(), 0, edges_buf_size));

  // upload source buffer2
  std::cout << "upload source buffer2" << std::endl;
  RT_CHECK(vx_copy_to_dev(src2_buffer, h_src2.data(), 0, nodes_buf_size));

  // upload source buffer3
  std::cout << "upload source buffer3" << std::endl;
  RT_CHECK(vx_copy_to_dev(src3_buffer, h_src3.data(), 0, nodes_buf_size));

  // upload source buffer4
  std::cout << "upload source buffer4" << std::endl;
  RT_CHECK(vx_copy_to_dev(src4_buffer, h_src4.data(), 0, nodes_buf_size));

  // upload source buffer5
  std::cout << "upload source buffer5" << std::endl;
  RT_CHECK(vx_copy_to_dev(src5_buffer, h_src5.data(), 0, nodes_buf_size));

  // upload cost/destination buffer
  std::cout << "upload destination buffer" << std::endl;
  RT_CHECK(vx_copy_to_dev(dst_buffer, h_dst.data(), 0, cost_buf_size));

  // upload program
  std::cout << "upload program" << std::endl;
  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));

  // upload kernel argument
  std::cout << "upload kernel argument" << std::endl;
  RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));


    // start device
    std::cout << "start device" << std::endl;
    RT_CHECK(vx_start(device, krnl_buffer, args_buffer));
    
    // wait for completion
    std::cout << "wait for completion" << std::endl;
    RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));


  // download destination buffer
  std::cout << "download destination buffer" << std::endl;
  RT_CHECK(vx_copy_from_dev(h_dst.data(), dst_buffer, 0, cost_buf_size));

  // verify result
  std::cout << "verify result" << std::endl;

  // Run Golden Results
  std::vector<int> cost(num_nodes);
  bfs_cpu(h_nodes.data(), h_edges.data(), num_nodes, 0, cost.data());
  /*bfs_cpu(h_nodes, h_edges, num_nodes, 0, cost);*/

  // Check for errors
  int errors = 0;
  for(uint32_t i = 0; i < num_nodes; i++){
    
    int cur = h_dst[i];
    int ref = cost[i];

    if (cur != ref) {
        std::cout << "error at result #" << std::dec << i
                  << std::hex << ": actual=" << cur << ", expected=" << ref << std::endl;
        ++errors;
    }
  }

  // cleanup
  std::cout << "cleanup" << std::endl;
  cleanup();

  if (errors != 0) {
    std::cout << "Found " << std::dec << errors << " errors!" << std::endl;
    std::cout << "FAILED!" << std::endl;
    return 1;
  }

  std::cout << "PASSED!" << std::endl;

  return 0;
}

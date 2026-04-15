#include <iostream>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <vortex.h>
#include "common.h"
#include "VX_types.h"

#define FLOAT_ULP 6

#define RT_CHECK(_expr)                                         \
   do {                                                         \
     int _ret = _expr;                                          \
     if (0 == _ret)                                             \
       break;                                                   \
     printf("Error: '%s' returned %d!\n", #_expr, (int)_ret);  \
     cleanup();                                                 \
     exit(-1);                                                  \
   } while (false)

template <typename Type> class Comparator {};
template <> class Comparator<float> {
public:
  static const char* type_str() { return "float"; }
  static float generate() { return static_cast<float>(rand()) / RAND_MAX; }
  static bool compare(float a, float b, int index, int errors) {
    union fi_t { float f; int32_t i; }; fi_t fa, fb; fa.f=a; fb.f=b;
    if (std::abs(fa.i-fb.i) > FLOAT_ULP) {
      if (errors<100) printf("*** error: [%d] expected=%f, actual=%f\n",index,b,a);
      return false;
    }
    return true;
  }
};

const char* kernel_file = "kernel.vxbin";
uint32_t size        = 64;
uint32_t num_threads = 4;    // -t: total threads per row (NUM_THREADS * NUM_WARPS)
uint32_t iteration   = 1;

vx_device_h device      = nullptr;
vx_buffer_h src0_buffer = nullptr;  // A
vx_buffer_h src1_buffer = nullptr;  // x ping
vx_buffer_h src2_buffer = nullptr;  // b
vx_buffer_h dst_buffer  = nullptr;  // x pong
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer_0 = nullptr;  // args pointing to src1 as x_old, dst as x_new
vx_buffer_h args_buffer_1 = nullptr;  // args pointing to dst  as x_old, src1 as x_new
kernel_arg_t kernel_arg_0 = {};
kernel_arg_t kernel_arg_1 = {};

static void show_usage() {
  std::cout << "Vortex jacobi (block reduce) Test." << std::endl;
  std::cout << "Usage: [-k kernel] [-n size] [-t num_threads] [-i iters] [-h help]" << std::endl;
}
static void parse_args(int argc, char **argv) {
  int c;
  while ((c = getopt(argc, argv, "n:t:i:k:h")) != -1) {
    switch (c) {
    case 'n': size        = atoi(optarg); break;
    case 't': num_threads = atoi(optarg); break;
    case 'i': iteration   = atoi(optarg); break;
    case 'k': kernel_file = optarg;       break;
    case 'h': show_usage(); exit(0);
    default:  show_usage(); exit(-1);
    }
  }
}
void cleanup() {
  if (device) {
    vx_mem_free(src0_buffer); vx_mem_free(src1_buffer);
    vx_mem_free(src2_buffer); vx_mem_free(dst_buffer);
    vx_mem_free(krnl_buffer);
    vx_mem_free(args_buffer_0); vx_mem_free(args_buffer_1);
    vx_dev_close(device);
  }
}

int main(int argc, char *argv[]) {
  parse_args(argc, argv);
  std::srand(50);

  if (num_threads == 0 || (num_threads & (num_threads-1)) != 0) {
    std::cerr << "num_threads must be a power of 2\n"; return -1;
  }

  std::cout << "open device connection" << std::endl;
  RT_CHECK(vx_dev_open(&device));

  uint32_t A_buf_size   = size * size * sizeof(TYPE);
  uint32_t dst_buf_size = size * sizeof(TYPE);

  // Each row gets num_threads total threads split across blocks.
  // block_dim_x = num_threads (all in one block per row) is simplest
  // and matches the __local_mem() reduction pattern.
  uint32_t block_dim_x = num_threads;
  uint32_t grid_dim_x  = size;  // one block per row

  std::cout << "size=" << size << " num_threads=" << num_threads
            << " block_dim=" << block_dim_x << " grid_dim=" << grid_dim_x
            << " iters=" << iteration << std::endl;

  uint32_t grid_dim[1]  = { grid_dim_x };
  uint32_t block_dim[1] = { block_dim_x };
  uint32_t lmem_size    = block_dim_x * sizeof(TYPE);

  // Allocate device memory
  RT_CHECK(vx_mem_alloc(device, A_buf_size,   VX_MEM_READ_WRITE, &src0_buffer));
  RT_CHECK(vx_mem_alloc(device, dst_buf_size, VX_MEM_READ_WRITE, &src1_buffer));
  RT_CHECK(vx_mem_alloc(device, dst_buf_size, VX_MEM_READ_WRITE, &src2_buffer));
  RT_CHECK(vx_mem_alloc(device, dst_buf_size, VX_MEM_READ_WRITE, &dst_buffer));

  uint64_t A_addr, x0_addr, b_addr, x1_addr;
  RT_CHECK(vx_mem_address(src0_buffer, &A_addr));
  RT_CHECK(vx_mem_address(src1_buffer, &x0_addr));  // ping
  RT_CHECK(vx_mem_address(src2_buffer, &b_addr));
  RT_CHECK(vx_mem_address(dst_buffer,  &x1_addr));  // pong

  // args_buffer_0: x_old=ping, x_new=pong
  kernel_arg_0.size      = size;
  kernel_arg_0.src0_addr = A_addr;
  kernel_arg_0.src1_addr = x0_addr;
  kernel_arg_0.src2_addr = b_addr;
  kernel_arg_0.dst_addr  = x1_addr;

  // args_buffer_1: x_old=pong, x_new=ping
  kernel_arg_1.size      = size;
  kernel_arg_1.src0_addr = A_addr;
  kernel_arg_1.src1_addr = x1_addr;
  kernel_arg_1.src2_addr = b_addr;
  kernel_arg_1.dst_addr  = x0_addr;

  // Generate data
  std::vector<TYPE> h_A(size*size), h_x(size, 0.0f), h_b(size), h_dst(size);
  for (uint32_t i = 0; i < size*size; ++i) h_A[i] = Comparator<TYPE>::generate();
  // Make diagonally dominant
  for (uint32_t i = 0; i < size; ++i) h_A[i*size+i] = (TYPE)size;
  for (uint32_t i = 0; i < size; ++i) h_b[i] = Comparator<TYPE>::generate();

  RT_CHECK(vx_copy_to_dev(src0_buffer, h_A.data(), 0, A_buf_size));
  RT_CHECK(vx_copy_to_dev(src1_buffer, h_x.data(), 0, dst_buf_size));  // x ping = 0
  RT_CHECK(vx_copy_to_dev(dst_buffer,  h_x.data(), 0, dst_buf_size));  // x pong = 0
  RT_CHECK(vx_copy_to_dev(src2_buffer, h_b.data(), 0, dst_buf_size));

  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));
  RT_CHECK(vx_upload_bytes(device, &kernel_arg_0, sizeof(kernel_arg_t), &args_buffer_0));
  RT_CHECK(vx_upload_bytes(device, &kernel_arg_1, sizeof(kernel_arg_t), &args_buffer_1));

  uint64_t total_cycles(0), total_instrs(0), cycles, instrs;

  for (uint32_t k = 0; k < iteration; ++k) {
    auto current_args = (k % 2 == 0) ? args_buffer_0 : args_buffer_1;

    std::cout << "start device iter=" << k << std::endl;
    RT_CHECK(vx_start_g(device, krnl_buffer, current_args, 1, grid_dim, block_dim, lmem_size));
    RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));

    RT_CHECK(vx_mpm_query(device, 0, VX_CSR_MCYCLE,   0, &cycles));
    RT_CHECK(vx_mpm_query(device, 0, VX_CSR_MINSTRET, 0, &instrs));
    total_cycles += cycles;
    total_instrs += instrs;
    printf("iter=%d\n", k);
  }

  // Download from whichever buffer was last written
  auto final_buf = (iteration % 2 == 0) ? src1_buffer : dst_buffer;
  std::cout << "download destination buffer" << std::endl;
  RT_CHECK(vx_copy_from_dev(h_dst.data(), final_buf, 0, dst_buf_size));

  // Verify against CPU reference
  std::cout << "verify result" << std::endl;
  std::vector<TYPE> h_gold(size, 0.0f), h_ref(size);
  jacobi_cpu(h_A.data(), h_gold.data(), h_ref.data(), h_b.data(), size, (int)iteration);

  int errors = 0;
  for (uint32_t i = 0; i < size; ++i)
    if (!Comparator<TYPE>::compare(h_dst[i], h_ref[i], i, errors)) ++errors;

  printf("total_cycles=%ld total_instrs=%ld\n", total_cycles, total_instrs);
  cleanup();

  if (errors) {
    std::cout << "Found " << errors << " errors! FAILED!" << std::endl;
    return 1;
  }
  std::cout << "PASSED!" << std::endl;
  return 0;
}

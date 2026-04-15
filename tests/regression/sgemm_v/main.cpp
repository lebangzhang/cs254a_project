#include <iostream>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <chrono>
#include <vortex.h>
#include <cmath>
#include "common.h"

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
template <> class Comparator<int> {
public:
  static const char* type_str() { return "integer"; }
  static int generate() { return rand(); }
  static bool compare(int a, int b, int index, int errors) {
    if (a != b) { if (errors<100) printf("*** error: [%d] expected=%d, actual=%d\n",index,b,a); return false; }
    return true;
  }
};
template <> class Comparator<float> {
public:
  static const char* type_str() { return "float"; }
  static float generate() { return static_cast<float>(rand()) / RAND_MAX; }
  static bool compare(float a, float b, int index, int errors) {
    union fi_t { float f; int32_t i; }; fi_t fa, fb; fa.f=a; fb.f=b;
    if (std::abs(fa.i-fb.i) > FLOAT_ULP) { if (errors<100) printf("*** error: [%d] expected=%f, actual=%f\n",index,b,a); return false; }
    return true;
  }
};

static void matmul_cpu(TYPE* out, const TYPE* A, const TYPE* B,
                       uint32_t width, uint32_t height) {
  for (uint32_t row = 0; row < height; ++row)
    for (uint32_t col = 0; col < width; ++col) {
      TYPE sum(0);
      for (uint32_t e = 0; e < width; ++e)
        sum += A[row*width+e] * B[e*width+col];
      out[row*width+col] = sum;
    }
}

const char* kernel_file = "kernel.vxbin";
uint32_t size = 32;
uint32_t vlen = VLEN / 32;

vx_device_h device      = nullptr;
vx_buffer_h A_buffer    = nullptr;
vx_buffer_h B_buffer    = nullptr;
vx_buffer_h C_buffer    = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

static void show_usage() {
  std::cout << "Vortex sgemm_v Test." << std::endl;
  std::cout << "Usage: [-k kernel] [-n size] [-v vlen] [-h help]" << std::endl;
}
static void parse_args(int argc, char **argv) {
  int c;
  while ((c = getopt(argc, argv, "n:v:k:h")) != -1) {
    switch (c) {
    case 'n': size = atoi(optarg); break;
    case 'v': vlen = atoi(optarg); break;
    case 'k': kernel_file = optarg; break;
    case 'h': show_usage(); exit(0);
    default:  show_usage(); exit(-1);
    }
  }
}
void cleanup() {
  if (device) {
    vx_mem_free(A_buffer); vx_mem_free(B_buffer); vx_mem_free(C_buffer);
    vx_mem_free(krnl_buffer); vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

int main(int argc, char *argv[]) {
  parse_args(argc, argv);
  std::srand(50);

  std::cout << "open device connection" << std::endl;
  RT_CHECK(vx_dev_open(&device));

  uint32_t size_sq  = size * size;
  uint32_t buf_size = size_sq * sizeof(TYPE);

  uint32_t num_col_tiles = (size + vlen - 1) / vlen;
  kernel_arg.vlen = vlen;

  std::cout << "data type: "      << Comparator<TYPE>::type_str() << std::endl;
  std::cout << "matrix size: "    << size << "x" << size          << std::endl;
  std::cout << "vlen: "           << vlen                         << std::endl;
  std::cout << "num_col_tiles: "  << num_col_tiles                << std::endl;

  kernel_arg.size = size;

  // global_dim: x = num_col_tiles (one per vector-width column slice)
  //             y = size          (one per row)
  // block_dim: chosen by vx_max_occupancy_grid
  uint32_t global_dim[2] = { num_col_tiles, size };
  uint32_t grid_dim[2], block_dim[2];
  RT_CHECK(vx_max_occupancy_grid(device, 2, global_dim, grid_dim, block_dim));

  std::cout << "grid_dim:  " << grid_dim[0]  << "x" << grid_dim[1]  << std::endl;
  std::cout << "block_dim: " << block_dim[0] << "x" << block_dim[1] << std::endl;

  std::cout << "allocate device memory" << std::endl;
  RT_CHECK(vx_mem_alloc(device, buf_size, VX_MEM_READ,  &A_buffer));
  RT_CHECK(vx_mem_address(A_buffer, &kernel_arg.A_addr));
  RT_CHECK(vx_mem_alloc(device, buf_size, VX_MEM_READ,  &B_buffer));
  RT_CHECK(vx_mem_address(B_buffer, &kernel_arg.B_addr));
  RT_CHECK(vx_mem_alloc(device, buf_size, VX_MEM_WRITE, &C_buffer));
  RT_CHECK(vx_mem_address(C_buffer, &kernel_arg.C_addr));

  std::cout << "A_addr=0x" << std::hex << kernel_arg.A_addr << std::endl;
  std::cout << "B_addr=0x" << std::hex << kernel_arg.B_addr << std::endl;
  std::cout << "C_addr=0x" << std::hex << kernel_arg.C_addr << std::endl;

  std::vector<TYPE> h_A(size_sq), h_B(size_sq), h_C(size_sq);
  for (uint32_t i = 0; i < size_sq; ++i) {
    h_A[i] = Comparator<TYPE>::generate();
    h_B[i] = Comparator<TYPE>::generate();
  }

  std::cout << "upload matrix A buffer" << std::endl;
  RT_CHECK(vx_copy_to_dev(A_buffer, h_A.data(), 0, buf_size));
  std::cout << "upload matrix B buffer" << std::endl;
  RT_CHECK(vx_copy_to_dev(B_buffer, h_B.data(), 0, buf_size));

  std::cout << "Upload kernel binary" << std::endl;
  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));
  std::cout << "upload kernel argument" << std::endl;
  RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));

  auto time_start = std::chrono::high_resolution_clock::now();
  std::cout << "start device" << std::endl;
  RT_CHECK(vx_start_g(device, krnl_buffer, args_buffer, 2, grid_dim, block_dim, 0));
  std::cout << "wait for completion" << std::endl;
  RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));
  auto time_end = std::chrono::high_resolution_clock::now();
  printf("Elapsed time: %lg ms\n",
    (double)std::chrono::duration_cast<std::chrono::milliseconds>(time_end-time_start).count());

  std::cout << "download destination buffer" << std::endl;
  RT_CHECK(vx_copy_from_dev(h_C.data(), C_buffer, 0, buf_size));

  std::cout << "verify result" << std::endl;
  int errors = 0;
  std::vector<TYPE> h_ref(size_sq);
  matmul_cpu(h_ref.data(), h_A.data(), h_B.data(), size, size);
  for (uint32_t i = 0; i < h_ref.size(); ++i)
    if (!Comparator<TYPE>::compare(h_C[i], h_ref[i], i, errors))
      ++errors;

  std::cout << "cleanup" << std::endl;
  cleanup();

  if (errors != 0) {
    std::cout << "Found " << std::dec << errors << " errors!" << std::endl;
    std::cout << "FAILED!" << std::endl;
    return errors;
  }
  std::cout << "PASSED!" << std::endl;
  return 0;
}


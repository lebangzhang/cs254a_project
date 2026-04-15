#include <iostream>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <vortex.h>
#include "common.h"

#define FLOAT_ULP 10

#define RT_CHECK(_expr)                                         \
   do {                                                         \
     int _ret = _expr;                                          \
     if (0 == _ret)                                             \
       break;                                                   \
     printf("Error: '%s' returned %d!\n", #_expr, (int)_ret);  \
     cleanup();                                                 \
     exit(-1);                                                  \
   } while (false)

///////////////////////////////////////////////////////////////////////////////

template <typename Type>
class Comparator {};

template <>
class Comparator<int> {
public:
  static const char* type_str() { return "integer"; }
  static int generate() { return rand(); }
  static bool compare(int a, int b, int index, int errors) {
    if (a != b) {
      if (errors < 100)
        printf("*** error: [%d] expected=%d, actual=%d\n", index, b, a);
      return false;
    }
    return true;
  }
};

template <>
class Comparator<float> {
public:
  static const char* type_str() { return "float"; }
  static float generate() { return static_cast<float>(rand()) / RAND_MAX; }
  static bool compare(float a, float b, int index, int errors) {
    union fi_t { float f; int32_t i; };
    fi_t fa, fb;
    fa.f = a; fb.f = b;
    if (std::abs(fa.i - fb.i) > FLOAT_ULP) {
      if (errors < 100)
        printf("*** error: [%d] expected=%f, actual=%f\n", index, b, a);
      return false;
    }
    return true;
  }
};

///////////////////////////////////////////////////////////////////////////////

const char* kernel_file = "kernel.vxbin";
uint32_t size            = 64;
uint32_t threads_per_row = 4;   // -t flag: threads collaborating on one row

vx_device_h device      = nullptr;
vx_buffer_h src0_buffer = nullptr;
vx_buffer_h src1_buffer = nullptr;
vx_buffer_h dst_buffer  = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

static void show_usage() {
  std::cout << "Vortex Softmax (warp-per-row) Test." << std::endl;
  std::cout << "Usage: [-k kernel] [-n size] [-t threads_per_row] [-h help]" << std::endl;
}

static void parse_args(int argc, char **argv) {
  int c;
  while ((c = getopt(argc, argv, "n:t:k:h")) != -1) {
    switch (c) {
    case 'n': size            = atoi(optarg); break;
    case 't': threads_per_row = atoi(optarg); break;
    case 'k': kernel_file     = optarg;       break;
    case 'h': show_usage(); exit(0);
    default:  show_usage(); exit(-1);
    }
  }
}

void cleanup() {
  if (device) {
    vx_mem_free(src0_buffer);
    vx_mem_free(src1_buffer);
    vx_mem_free(dst_buffer);
    vx_mem_free(krnl_buffer);
    vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

int main(int argc, char *argv[]) {
  parse_args(argc, argv);
  std::srand(50);

  std::cout << "open device connection" << std::endl;
  RT_CHECK(vx_dev_open(&device));

  uint32_t num_cols   = size;
  uint32_t num_rows   = size;
  uint32_t total_size = num_cols * num_rows;
  uint32_t buf_size   = total_size * sizeof(TYPE);

  std::cout << "number of rows: "      << num_rows        << std::endl;
  std::cout << "number of cols: "      << num_cols        << std::endl;
  std::cout << "threads per row: "     << threads_per_row << std::endl;
  std::cout << "data type: "           << Comparator<TYPE>::type_str() << std::endl;
  std::cout << "buffer size: "         << buf_size << " bytes" << std::endl;

  kernel_arg.num_cols = num_cols;
  kernel_arg.num_rows = num_rows;

  // One block per row, threads_per_row threads per block
  uint32_t grid_dim[1]  = { num_rows };
  uint32_t block_dim[1] = { threads_per_row };
  uint32_t lmem_size    = threads_per_row * sizeof(TYPE);

  // allocate device memory
  std::cout << "allocate device memory" << std::endl;
  RT_CHECK(vx_mem_alloc(device, buf_size, VX_MEM_READ_WRITE, &src0_buffer));
  RT_CHECK(vx_mem_address(src0_buffer, &kernel_arg.src0_addr));
  RT_CHECK(vx_mem_alloc(device, buf_size, VX_MEM_READ_WRITE, &src1_buffer));
  RT_CHECK(vx_mem_address(src1_buffer, &kernel_arg.src1_addr));
  RT_CHECK(vx_mem_alloc(device, buf_size, VX_MEM_READ_WRITE, &dst_buffer));
  RT_CHECK(vx_mem_address(dst_buffer,  &kernel_arg.dst_addr));

  std::cout << "dev_src0=0x" << std::hex << kernel_arg.src0_addr << std::endl;
  std::cout << "dev_src1=0x" << std::hex << kernel_arg.src1_addr << std::endl;
  std::cout << "dev_dst=0x"  << std::hex << kernel_arg.dst_addr  << std::endl;

  // allocate host buffers
  std::vector<TYPE> h_src0(total_size);
  std::vector<TYPE> h_src1(total_size);
  std::vector<TYPE> h_dst(total_size);

  for (uint32_t i = 0; i < total_size; ++i) {
    h_src0[i] = Comparator<TYPE>::generate();
    h_src1[i] = Comparator<TYPE>::generate();
  }

  std::cout << "upload source buffer0" << std::endl;
  RT_CHECK(vx_copy_to_dev(src0_buffer, h_src0.data(), 0, buf_size));
  std::cout << "upload source buffer1" << std::endl;
  RT_CHECK(vx_copy_to_dev(src1_buffer, h_src1.data(), 0, buf_size));

  std::cout << "upload program" << std::endl;
  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));
  std::cout << "upload kernel argument" << std::endl;
  RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));

  std::cout << "start device" << std::endl;
  RT_CHECK(vx_start_g(device, krnl_buffer, args_buffer, 1, grid_dim, block_dim, lmem_size));

  std::cout << "wait for completion" << std::endl;
  RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));

  std::cout << "download destination buffer" << std::endl;
  RT_CHECK(vx_copy_from_dev(h_dst.data(), dst_buffer, 0, buf_size));

  // verify result
  std::cout << "verify result" << std::endl;
  int errors = 0;
  for (uint32_t i = 0; i < num_rows; ++i) {
    uint32_t tid = i * num_cols;
    TYPE max = 0.0, sum = 0.0;

    for (uint32_t k = 0; k < num_cols; k++)
      if (h_src0[tid + k] > max) max = h_src0[tid + k];
    for (uint32_t k = 0; k < num_cols; k++)
      sum += exp(h_src0[tid + k] - max);
    for (uint32_t k = 0; k < num_cols; k++) {
      auto ref = exp(h_src0[tid + k] - max) / sum;
      if (!Comparator<TYPE>::compare(h_dst[tid + k], ref, i, errors))
        ++errors;
    }
  }

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

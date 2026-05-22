#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <unistd.h>
#include <vector>
#include <vortex.h>
#include "common.h"

#define RT_CHECK(_expr)                                        \
  do {                                                         \
    int _ret = _expr;                                          \
    if (0 == _ret)                                             \
      break;                                                   \
    printf("Error: '%s' returned %d!\n", #_expr, (int)_ret);  \
    cleanup();                                                 \
    exit(-1);                                                  \
  } while (false)

const char* kernel_file = "kernel.vxbin";
static constexpr uint32_t DATA_SIZE = 48;
static constexpr float SENTINEL = -123.0f;

vx_device_h device = nullptr;
vx_buffer_h scalar_buffer = nullptr;
vx_buffer_h src_buffer = nullptr;
vx_buffer_h dst_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

void cleanup() {
  if (device) {
    vx_mem_free(scalar_buffer);
    vx_mem_free(src_buffer);
    vx_mem_free(dst_buffer);
    vx_mem_free(krnl_buffer);
    vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

static void show_usage() {
  std::cout << "Vortex RVV vfmv Test." << std::endl;
  std::cout << "Usage: [-k kernel] [-h help]" << std::endl;
}

static void parse_args(int argc, char** argv) {
  int c;
  while ((c = getopt(argc, argv, "n:v:k:h")) != -1) {
    switch (c) {
    case 'n': break;
    case 'v': break;
    case 'k': kernel_file = optarg; break;
    case 'h': show_usage(); exit(0);
    default: show_usage(); exit(-1);
    }
  }
}

int main(int argc, char* argv[]) {
  parse_args(argc, argv);

  uint32_t data_bytes = DATA_SIZE * sizeof(float);
  kernel_arg.data_size = DATA_SIZE;

  RT_CHECK(vx_dev_open(&device));
  RT_CHECK(vx_mem_alloc(device, sizeof(float), VX_MEM_READ, &scalar_buffer));
  RT_CHECK(vx_mem_address(scalar_buffer, &kernel_arg.scalar_addr));
  RT_CHECK(vx_mem_alloc(device, data_bytes, VX_MEM_READ, &src_buffer));
  RT_CHECK(vx_mem_address(src_buffer, &kernel_arg.src_addr));
  RT_CHECK(vx_mem_alloc(device, data_bytes, VX_MEM_WRITE, &dst_buffer));
  RT_CHECK(vx_mem_address(dst_buffer, &kernel_arg.dst_addr));

  float scalar = 1.5f;
  std::vector<float> h_src(DATA_SIZE);
  std::vector<float> h_dst(DATA_SIZE, SENTINEL);
  std::vector<float> h_expected(DATA_SIZE, SENTINEL);

  for (uint32_t i = 0; i < DATA_SIZE; ++i)
    h_src[i] = 0.25f * float(i + 1);

  h_expected[0] = h_src[0];
  h_expected[1] = h_src[8];
  h_expected[2] = h_src[16];
  h_expected[3] = h_src[32];
  for (uint32_t i = 0; i < 4; ++i)
    h_expected[8 + i] = scalar;
  for (uint32_t i = 0; i < 8; ++i)
    h_expected[16 + i] = scalar;

  RT_CHECK(vx_copy_to_dev(scalar_buffer, &scalar, 0, sizeof(float)));
  RT_CHECK(vx_copy_to_dev(src_buffer, h_src.data(), 0, data_bytes));
  RT_CHECK(vx_copy_to_dev(dst_buffer, h_dst.data(), 0, data_bytes));
  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));
  RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));

  uint32_t grid_dim[2] = {1, 1};
  uint32_t block_dim[2] = {1, 1};

  auto time_start = std::chrono::high_resolution_clock::now();
  RT_CHECK(vx_start_g(device, krnl_buffer, args_buffer, 2, grid_dim, block_dim, 0));
  RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));
  auto time_end = std::chrono::high_resolution_clock::now();
  printf("Elapsed time: %lg ms\n",
    (double)std::chrono::duration_cast<std::chrono::milliseconds>(time_end-time_start).count());

  RT_CHECK(vx_copy_from_dev(h_dst.data(), dst_buffer, 0, data_bytes));

  int errors = 0;
  for (uint32_t i = 0; i < DATA_SIZE; ++i) {
    if (h_dst[i] != h_expected[i]) {
      if (errors < 100)
        printf("*** error: [%u] expected=%f, actual=%f\n", i, h_expected[i], h_dst[i]);
      ++errors;
    }
  }

  cleanup();
  if (errors != 0) {
    std::cout << "FAILED!" << std::endl;
    return errors;
  }
  std::cout << "PASSED!" << std::endl;
  return 0;
}

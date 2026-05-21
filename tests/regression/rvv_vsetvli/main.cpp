#include <algorithm>
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
uint32_t avl = VLEN / 32;

vx_device_h device = nullptr;
vx_buffer_h out_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

void cleanup() {
  if (device) {
    vx_mem_free(out_buffer);
    vx_mem_free(krnl_buffer);
    vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

static void show_usage() {
  std::cout << "Vortex RVV vsetvli Test." << std::endl;
  std::cout << "Usage: [-k kernel] [-n avl] [-h help]" << std::endl;
}

static void parse_args(int argc, char** argv) {
  int c;
  while ((c = getopt(argc, argv, "n:k:h")) != -1) {
    switch (c) {
    case 'n': avl = atoi(optarg); break;
    case 'k': kernel_file = optarg; break;
    case 'h': show_usage(); exit(0);
    default: show_usage(); exit(-1);
    }
  }
}

int main(int argc, char* argv[]) {
  parse_args(argc, argv);

  kernel_arg.avl = avl;
  kernel_arg.expected_vl = std::min<uint32_t>(avl, VLEN / 32);

  std::cout << "open device connection" << std::endl;
  RT_CHECK(vx_dev_open(&device));

  std::vector<uint32_t> h_out(2, 0);
  RT_CHECK(vx_mem_alloc(device, h_out.size() * sizeof(uint32_t), VX_MEM_WRITE, &out_buffer));
  RT_CHECK(vx_mem_address(out_buffer, &kernel_arg.out_addr));
  RT_CHECK(vx_copy_to_dev(out_buffer, h_out.data(), 0, h_out.size() * sizeof(uint32_t)));

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

  RT_CHECK(vx_copy_from_dev(h_out.data(), out_buffer, 0, h_out.size() * sizeof(uint32_t)));

  int errors = 0;
  if (h_out[0] != kernel_arg.expected_vl) {
    printf("*** error: expected vl=%u, actual vl=%u\n", kernel_arg.expected_vl, h_out[0]);
    ++errors;
  }
  if (h_out[1] != kernel_arg.expected_vl) {
    printf("*** error: expected mirror=%u, actual mirror=%u\n", kernel_arg.expected_vl, h_out[1]);
    ++errors;
  }

  cleanup();
  if (errors != 0) {
    std::cout << "FAILED!" << std::endl;
    return errors;
  }
  std::cout << "PASSED!" << std::endl;
  return 0;
}

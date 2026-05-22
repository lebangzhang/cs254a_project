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

static constexpr uint32_t DATA_SIZE = 768;
static constexpr uint32_t INDEX_COUNT = 8;
static constexpr uint8_t SENTINEL = 0xa5;

const char* kernel_file = "kernel.vxbin";

vx_device_h device = nullptr;
vx_buffer_h src_buffer = nullptr;
vx_buffer_h dst_buffer = nullptr;
vx_buffer_h idx_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

void cleanup() {
  if (device) {
    vx_mem_free(src_buffer);
    vx_mem_free(dst_buffer);
    vx_mem_free(idx_buffer);
    vx_mem_free(krnl_buffer);
    vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

static void show_usage() {
  std::cout << "Vortex RVV vector LSU Test." << std::endl;
  std::cout << "Usage: [-k kernel] [-n ignored] [-v ignored] [-h help]" << std::endl;
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

static void copy_range(std::vector<uint8_t>& expected,
                       const std::vector<uint8_t>& src,
                       uint32_t dst_offset,
                       uint32_t src_offset,
                       uint32_t bytes) {
  for (uint32_t i = 0; i < bytes; ++i)
    expected[dst_offset + i] = src[src_offset + i];
}

int main(int argc, char* argv[]) {
  parse_args(argc, argv);

  std::vector<uint8_t> h_src(DATA_SIZE);
  std::vector<uint8_t> h_dst(DATA_SIZE, SENTINEL);
  std::vector<uint8_t> h_expected(DATA_SIZE, SENTINEL);
  std::vector<uint32_t> h_idx(INDEX_COUNT);

  for (uint32_t i = 0; i < DATA_SIZE; ++i)
    h_src[i] = uint8_t((i * 17 + 3) & 0xff);
  for (uint32_t i = 0; i < INDEX_COUNT; ++i)
    h_idx[i] = (INDEX_COUNT - 1 - i) * sizeof(uint32_t);

  copy_range(h_expected, h_src, 0, 0, 16);       // e8, m1
  copy_range(h_expected, h_src, 64, 64, 32);     // e16, m1
  copy_range(h_expected, h_src, 128, 128, 32);   // e32, m1
  copy_range(h_expected, h_src, 192, 192, 32);   // e64, m1
  copy_range(h_expected, h_src, 256, 256, 64);   // e32, m2

  for (uint32_t i = 0; i < INDEX_COUNT; ++i) {
    copy_range(h_expected, h_src, 384 + i * sizeof(uint32_t),
               384 + i * 8, sizeof(uint32_t));
    copy_range(h_expected, h_src, 512 + i * sizeof(uint32_t),
               512 + h_idx[i], sizeof(uint32_t));
  }
  copy_range(h_expected, h_src, 640, 640, 64);   // vlseg2e32/vsseg2e32

  kernel_arg.data_size = DATA_SIZE;
  kernel_arg.index_count = INDEX_COUNT;

  RT_CHECK(vx_dev_open(&device));
  RT_CHECK(vx_mem_alloc(device, h_src.size(), VX_MEM_READ, &src_buffer));
  RT_CHECK(vx_mem_address(src_buffer, &kernel_arg.src_addr));
  RT_CHECK(vx_mem_alloc(device, h_dst.size(), VX_MEM_WRITE, &dst_buffer));
  RT_CHECK(vx_mem_address(dst_buffer, &kernel_arg.dst_addr));
  RT_CHECK(vx_mem_alloc(device, h_idx.size() * sizeof(uint32_t), VX_MEM_READ, &idx_buffer));
  RT_CHECK(vx_mem_address(idx_buffer, &kernel_arg.idx_addr));

  RT_CHECK(vx_copy_to_dev(src_buffer, h_src.data(), 0, h_src.size()));
  RT_CHECK(vx_copy_to_dev(dst_buffer, h_dst.data(), 0, h_dst.size()));
  RT_CHECK(vx_copy_to_dev(idx_buffer, h_idx.data(), 0, h_idx.size() * sizeof(uint32_t)));
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

  RT_CHECK(vx_copy_from_dev(h_dst.data(), dst_buffer, 0, h_dst.size()));

  int errors = 0;
  for (uint32_t i = 0; i < DATA_SIZE; ++i) {
    if (h_dst[i] != h_expected[i]) {
      if (errors < 100)
        printf("*** error: byte[%u] expected=0x%02x, actual=0x%02x\n",
               i, h_expected[i], h_dst[i]);
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

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <unistd.h>
#include <vector>
#include <vortex.h>
#include "common.h"

#define FLOAT_ULP 8
#define DOUBLE_ULP 16

#define RT_CHECK(_expr)                                        \
  do {                                                         \
    int _ret = _expr;                                          \
    if (0 == _ret)                                             \
      break;                                                   \
    printf("Error: '%s' returned %d!\n", #_expr, (int)_ret);  \
    cleanup();                                                 \
    exit(-1);                                                  \
  } while (false)

static bool nearly_equal(float a, float b) {
  if (a == b)
    return true;
  if (std::signbit(a) != std::signbit(b))
    return false;
  uint32_t ia, ib;
  std::memcpy(&ia, &a, sizeof(a));
  std::memcpy(&ib, &b, sizeof(b));
  uint32_t diff = ia > ib ? ia - ib : ib - ia;
  return diff <= FLOAT_ULP;
}

static bool nearly_equal(double a, double b) {
  if (a == b)
    return true;
  if (std::signbit(a) != std::signbit(b))
    return false;
  uint64_t ia, ib;
  std::memcpy(&ia, &a, sizeof(a));
  std::memcpy(&ib, &b, sizeof(b));
  uint64_t diff = ia > ib ? ia - ib : ib - ia;
  return diff <= DOUBLE_ULP;
}

const char* kernel_file = "kernel.vxbin";
static constexpr uint32_t F32_SIZE = 128;
static constexpr uint32_t F64_SIZE = 64;
static constexpr float F32_SENTINEL = -123.0f;
static constexpr double F64_SENTINEL = -123.0;

vx_device_h device = nullptr;
vx_buffer_h f32_acc_buffer = nullptr;
vx_buffer_h f32_lhs_buffer = nullptr;
vx_buffer_h f32_rhs_buffer = nullptr;
vx_buffer_h f32_dst_buffer = nullptr;
vx_buffer_h f64_acc_buffer = nullptr;
vx_buffer_h f64_lhs_buffer = nullptr;
vx_buffer_h f64_rhs_buffer = nullptr;
vx_buffer_h f64_dst_buffer = nullptr;
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

void cleanup() {
  if (device) {
    vx_mem_free(f32_acc_buffer);
    vx_mem_free(f32_lhs_buffer);
    vx_mem_free(f32_rhs_buffer);
    vx_mem_free(f32_dst_buffer);
    vx_mem_free(f64_acc_buffer);
    vx_mem_free(f64_lhs_buffer);
    vx_mem_free(f64_rhs_buffer);
    vx_mem_free(f64_dst_buffer);
    vx_mem_free(krnl_buffer);
    vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

static void show_usage() {
  std::cout << "Vortex RVV vfmacc Test." << std::endl;
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

template <typename T>
static void set_ref_range(std::vector<T>& ref,
                          const std::vector<T>& acc,
                          const std::vector<T>& lhs,
                          const std::vector<T>& rhs,
                          uint32_t offset,
                          uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t idx = offset + i;
    ref[idx] = std::fma(lhs[idx], rhs[idx], acc[idx]);
  }
}

template <typename T>
static void set_ref_range_scalar(std::vector<T>& ref,
                                 const std::vector<T>& acc,
                                 T scalar,
                                 const std::vector<T>& rhs,
                                 uint32_t offset,
                                 uint32_t count) {
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t idx = offset + i;
    ref[idx] = std::fma(scalar, rhs[idx], acc[idx]);
  }
}

int main(int argc, char* argv[]) {
  parse_args(argc, argv);

  uint32_t f32_bytes = F32_SIZE * sizeof(float);
  uint32_t f64_bytes = F64_SIZE * sizeof(double);
  kernel_arg.f32_size = F32_SIZE;
  kernel_arg.f64_size = F64_SIZE;

  RT_CHECK(vx_dev_open(&device));
  RT_CHECK(vx_mem_alloc(device, f32_bytes, VX_MEM_READ, &f32_acc_buffer));
  RT_CHECK(vx_mem_address(f32_acc_buffer, &kernel_arg.f32_acc_addr));
  RT_CHECK(vx_mem_alloc(device, f32_bytes, VX_MEM_READ, &f32_lhs_buffer));
  RT_CHECK(vx_mem_address(f32_lhs_buffer, &kernel_arg.f32_lhs_addr));
  RT_CHECK(vx_mem_alloc(device, f32_bytes, VX_MEM_READ, &f32_rhs_buffer));
  RT_CHECK(vx_mem_address(f32_rhs_buffer, &kernel_arg.f32_rhs_addr));
  RT_CHECK(vx_mem_alloc(device, f32_bytes, VX_MEM_WRITE, &f32_dst_buffer));
  RT_CHECK(vx_mem_address(f32_dst_buffer, &kernel_arg.f32_dst_addr));

  RT_CHECK(vx_mem_alloc(device, f64_bytes, VX_MEM_READ, &f64_acc_buffer));
  RT_CHECK(vx_mem_address(f64_acc_buffer, &kernel_arg.f64_acc_addr));
  RT_CHECK(vx_mem_alloc(device, f64_bytes, VX_MEM_READ, &f64_lhs_buffer));
  RT_CHECK(vx_mem_address(f64_lhs_buffer, &kernel_arg.f64_lhs_addr));
  RT_CHECK(vx_mem_alloc(device, f64_bytes, VX_MEM_READ, &f64_rhs_buffer));
  RT_CHECK(vx_mem_address(f64_rhs_buffer, &kernel_arg.f64_rhs_addr));
  RT_CHECK(vx_mem_alloc(device, f64_bytes, VX_MEM_WRITE, &f64_dst_buffer));
  RT_CHECK(vx_mem_address(f64_dst_buffer, &kernel_arg.f64_dst_addr));

  std::vector<float> h_f32_acc(F32_SIZE), h_f32_lhs(F32_SIZE), h_f32_rhs(F32_SIZE);
  std::vector<float> h_f32_dst(F32_SIZE, F32_SENTINEL);
  std::vector<float> h_f32_ref(F32_SIZE, F32_SENTINEL);
  for (uint32_t i = 0; i < F32_SIZE; ++i) {
    h_f32_acc[i] = 0.5f + 0.03125f * float(i);
    h_f32_lhs[i] = 1.25f + 0.015625f * float(i);
    h_f32_rhs[i] = 2.0f + 0.0625f * float(i);
  }
  h_f32_lhs[120] = 1.5f;

  std::vector<double> h_f64_acc(F64_SIZE), h_f64_lhs(F64_SIZE), h_f64_rhs(F64_SIZE);
  std::vector<double> h_f64_dst(F64_SIZE, F64_SENTINEL);
  std::vector<double> h_f64_ref(F64_SIZE, F64_SENTINEL);
  for (uint32_t i = 0; i < F64_SIZE; ++i) {
    h_f64_acc[i] = 0.75 + 0.015625 * double(i);
    h_f64_lhs[i] = 1.125 + 0.0078125 * double(i);
    h_f64_rhs[i] = 1.75 + 0.03125 * double(i);
  }
  h_f64_lhs[60] = 1.25;

  set_ref_range(h_f32_ref, h_f32_acc, h_f32_lhs, h_f32_rhs, 0, 4);
  set_ref_range(h_f32_ref, h_f32_acc, h_f32_lhs, h_f32_rhs, 16, 8);
  set_ref_range(h_f32_ref, h_f32_acc, h_f32_lhs, h_f32_rhs, 32, 16);
  set_ref_range_scalar(h_f32_ref, h_f32_acc, h_f32_lhs[120], h_f32_rhs, 64, 4);
  set_ref_range_scalar(h_f32_ref, h_f32_acc, h_f32_lhs[120], h_f32_rhs, 80, 8);
  set_ref_range_scalar(h_f32_ref, h_f32_acc, h_f32_lhs[120], h_f32_rhs, 96, 16);

  set_ref_range(h_f64_ref, h_f64_acc, h_f64_lhs, h_f64_rhs, 0, 2);
  set_ref_range(h_f64_ref, h_f64_acc, h_f64_lhs, h_f64_rhs, 8, 4);
  set_ref_range(h_f64_ref, h_f64_acc, h_f64_lhs, h_f64_rhs, 16, 8);
  set_ref_range_scalar(h_f64_ref, h_f64_acc, h_f64_lhs[60], h_f64_rhs, 32, 2);
  set_ref_range_scalar(h_f64_ref, h_f64_acc, h_f64_lhs[60], h_f64_rhs, 40, 4);
  set_ref_range_scalar(h_f64_ref, h_f64_acc, h_f64_lhs[60], h_f64_rhs, 48, 8);

  RT_CHECK(vx_copy_to_dev(f32_acc_buffer, h_f32_acc.data(), 0, f32_bytes));
  RT_CHECK(vx_copy_to_dev(f32_lhs_buffer, h_f32_lhs.data(), 0, f32_bytes));
  RT_CHECK(vx_copy_to_dev(f32_rhs_buffer, h_f32_rhs.data(), 0, f32_bytes));
  RT_CHECK(vx_copy_to_dev(f32_dst_buffer, h_f32_dst.data(), 0, f32_bytes));
  RT_CHECK(vx_copy_to_dev(f64_acc_buffer, h_f64_acc.data(), 0, f64_bytes));
  RT_CHECK(vx_copy_to_dev(f64_lhs_buffer, h_f64_lhs.data(), 0, f64_bytes));
  RT_CHECK(vx_copy_to_dev(f64_rhs_buffer, h_f64_rhs.data(), 0, f64_bytes));
  RT_CHECK(vx_copy_to_dev(f64_dst_buffer, h_f64_dst.data(), 0, f64_bytes));
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

  RT_CHECK(vx_copy_from_dev(h_f32_dst.data(), f32_dst_buffer, 0, f32_bytes));
  RT_CHECK(vx_copy_from_dev(h_f64_dst.data(), f64_dst_buffer, 0, f64_bytes));

  int errors = 0;
  for (uint32_t i = 0; i < F32_SIZE; ++i) {
    if (!nearly_equal(h_f32_dst[i], h_f32_ref[i])) {
      if (errors < 100)
        printf("*** error f32: [%u] expected=%f, actual=%f\n", i, h_f32_ref[i], h_f32_dst[i]);
      ++errors;
    }
  }
  for (uint32_t i = 0; i < F64_SIZE; ++i) {
    if (!nearly_equal(h_f64_dst[i], h_f64_ref[i])) {
      if (errors < 100)
        printf("*** error f64: [%u] expected=%lf, actual=%lf\n", i, h_f64_ref[i], h_f64_dst[i]);
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

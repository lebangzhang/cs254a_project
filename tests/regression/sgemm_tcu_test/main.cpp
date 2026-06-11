#include "common.h"
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <rvfloats.h>
#include <tensor_cfg.h>
#include <util.h>
#include <vector>
#include <vortex.h>

namespace vt = vortex::tensor;
using cfg = vt::wmma_config_t<NUM_THREADS, vt::ITYPE, vt::OTYPE>;
using itype_t = typename vt::ITYPE::dtype;
using otype_t = typename vt::OTYPE::dtype;

static const char* kernel_file = "kernel.vxbin";

static vx_device_h device = nullptr;
static vx_buffer_h A_buffer = nullptr;
static vx_buffer_h B_buffer = nullptr;
static vx_buffer_h C_buffer = nullptr;
static vx_buffer_h krnl_buffer = nullptr;
static vx_buffer_h args_buffer = nullptr;

static bool almost_equal(float actual, float expected) {
  const float diff = std::fabs(actual - expected);
  const float tol = 1e-4f + 1e-3f * std::fabs(expected);
  return diff <= tol;
}

static itype_t generate_input_value() {
  const float fvalue = static_cast<float>(std::rand()) / RAND_MAX;
  return rv_ftotf32_s(vortex::bit_cast<uint32_t>(fvalue), 0, nullptr);
}

static float to_float(itype_t value) {
  return vortex::bit_cast<float>(rv_tf32tof_s(value, 0, nullptr));
}

static void cleanup() {
  if (device) {
    vx_mem_free(A_buffer);
    vx_mem_free(B_buffer);
    vx_mem_free(C_buffer);
    vx_mem_free(krnl_buffer);
    vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

#define RT_CHECK(_expr)                                       \
  do {                                                        \
    int _ret = _expr;                                        \
    if (0 == _ret)                                           \
      break;                                                  \
    printf("Error: '%s' returned %d!\n", #_expr, (int)_ret); \
    cleanup();                                                \
    return -1;                                                \
  } while (false)

int main() {
  constexpr uint32_t M = cfg::tileM;
  constexpr uint32_t N = cfg::tileN;
  constexpr uint32_t K = cfg::tileK;

  std::vector<itype_t> A(M * K);
  std::vector<itype_t> B(K * N);
  std::vector<otype_t> C(M * N, 0.0f);
  std::vector<float> ref(M * N, 0.0f);

  std::srand(50);
  for (uint32_t i = 0; i < A.size(); ++i)
    A[i] = generate_input_value();
  for (uint32_t i = 0; i < B.size(); ++i)
    B[i] = generate_input_value();

  for (uint32_t i = 0; i < M; ++i) {
    for (uint32_t j = 0; j < N; ++j) {
      float sum = 0.0f;
      for (uint32_t k = 0; k < K; ++k)
        sum += to_float(A[i * K + k]) * to_float(B[k * N + j]);
      ref[i * N + j] = sum;
    }
  }

  kernel_arg_t kernel_arg{};

  RT_CHECK(vx_dev_open(&device));

  uint64_t isa_flags = 0;
  RT_CHECK(vx_dev_caps(device, VX_CAPS_ISA_FLAGS, &isa_flags));
  if ((isa_flags & VX_ISA_EXT_TCU) == 0) {
    std::cout << "TCU extension not supported!" << std::endl;
    cleanup();
    return -1;
  }

  uint64_t NT = 0;
  RT_CHECK(vx_dev_caps(device, VX_CAPS_NUM_THREADS, &NT));
  if (NT != NUM_THREADS) {
    std::cout << "Error: device warp size (" << NT
              << ") must match NUM_THREADS=" << NUM_THREADS << "!" << std::endl;
    cleanup();
    return -1;
  }

  RT_CHECK(vx_mem_alloc(device, A.size() * sizeof(itype_t), VX_MEM_READ, &A_buffer));
  RT_CHECK(vx_mem_address(A_buffer, &kernel_arg.A_addr));
  RT_CHECK(vx_mem_alloc(device, B.size() * sizeof(itype_t), VX_MEM_READ, &B_buffer));
  RT_CHECK(vx_mem_address(B_buffer, &kernel_arg.B_addr));
  RT_CHECK(vx_mem_alloc(device, C.size() * sizeof(otype_t), VX_MEM_WRITE, &C_buffer));
  RT_CHECK(vx_mem_address(C_buffer, &kernel_arg.C_addr));

  RT_CHECK(vx_copy_to_dev(A_buffer, A.data(), 0, A.size() * sizeof(itype_t)));
  RT_CHECK(vx_copy_to_dev(B_buffer, B.data(), 0, B.size() * sizeof(itype_t)));
  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));
  RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));

  uint32_t grid_dim[] = {1, 1};
  uint32_t block_dim[] = {NUM_THREADS, 1};
  RT_CHECK(vx_start_g(device, krnl_buffer, args_buffer, 2, grid_dim, block_dim, 0));
  RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));
  RT_CHECK(vx_copy_from_dev(C.data(), C_buffer, 0, C.size() * sizeof(otype_t)));

  int errors = 0;
  for (uint32_t i = 0; i < C.size(); ++i) {
    if (!almost_equal(C[i], ref[i])) {
      if (errors < 10)
        printf("*** error: [%u] expected=%f, actual=%f\n", i, ref[i], C[i]);
      ++errors;
    }
  }

  cleanup();
  if (errors != 0) {
    std::cout << "Found " << errors << " errors!" << std::endl;
    return 1;
  }

  std::cout << "PASSED!" << std::endl;
  return 0;
}

#include "common.h"

#include <cmath>
#include <iostream>
#include <tensor_cfg.h>
#include <vector>
#include <vortex.h>

namespace vt = vortex::tensor;
using cfg = vt::wmma_config_t<NUM_THREADS>;

namespace {

constexpr uint32_t kMaxVregs = 32;
constexpr uint32_t kABase = 0;
constexpr uint32_t kB0Base = kABase + cfg::tileM;
constexpr uint32_t kB1Base = kB0Base + cfg::tileK;
constexpr uint32_t kUniqueDstBase = kB1Base + cfg::tileK;
constexpr bool kHasUniqueDst = (kUniqueDstBase + cfg::tileM) <= kMaxVregs;

constexpr uint32_t slot_elem_count() {
  return cfg::tileM * cfg::tileN;
}

float pattern_a(uint32_t idx) {
  return static_cast<float>(static_cast<int>(idx % 13) - 6) * 0.25f;
}

float pattern_b0(uint32_t idx) {
  return static_cast<float>(static_cast<int>(idx % 11) - 5) * 0.125f;
}

float pattern_b1(uint32_t idx) {
  return static_cast<float>(static_cast<int>((idx * 3) % 17) - 8) * 0.0625f;
}

std::vector<float> matmul(const std::vector<float>& A,
                          const std::vector<float>& B,
                          uint32_t M,
                          uint32_t N,
                          uint32_t K) {
  std::vector<float> out(M * N, 0.0f);
  for (uint32_t i = 0; i < M; ++i) {
    for (uint32_t j = 0; j < N; ++j) {
      float sum = 0.0f;
      for (uint32_t k = 0; k < K; ++k)
        sum += A[i * K + k] * B[k * N + j];
      out[i * N + j] = sum;
    }
  }
  return out;
}

std::vector<float> take_top_rows(const std::vector<float>& Mtx,
                                 uint32_t rows,
                                 uint32_t cols) {
  std::vector<float> out(rows * cols, 0.0f);
  for (uint32_t i = 0; i < rows; ++i) {
    for (uint32_t j = 0; j < cols; ++j)
      out[i * cols + j] = Mtx[i * cols + j];
  }
  return out;
}

const char* case_name(uint32_t bit) {
  switch (bit) {
  case kCaseUniqueDst: return "unique-dst";
  case kCaseDstEqSrcA: return "dst-eq-srcA";
  case kCaseDstEqSrcB: return "dst-eq-srcB";
  case kCaseDstEqSrcATwice: return "dst-eq-srcA-twice";
  case kCaseDstEqSrcBTwice: return "dst-eq-srcB-twice";
  default:             return "unknown";
  }
}

uint32_t slot_index(uint32_t bit) {
  switch (bit) {
  case kCaseUniqueDst: return 0;
  case kCaseDstEqSrcA: return 1;
  case kCaseDstEqSrcB: return 2;
  case kCaseDstEqSrcATwice: return 3;
  case kCaseDstEqSrcBTwice: return 4;
  default:             return 0;
  }
}

uint32_t enabled_case_mask() {
  uint32_t mask = 0;
  if constexpr (kHasUniqueDst)
    mask |= kCaseUniqueDst;
  mask |= kCaseDstEqSrcA | kCaseDstEqSrcB | kCaseDstEqSrcATwice | kCaseDstEqSrcBTwice;
  return mask;
}

} // namespace

static const char* kernel_file = "kernel.vxbin";

static vx_device_h device = nullptr;
static vx_buffer_h A_buffer = nullptr;
static vx_buffer_h B0_buffer = nullptr;
static vx_buffer_h B1_buffer = nullptr;
static vx_buffer_h C_buffer = nullptr;
static vx_buffer_h krnl_buffer = nullptr;
static vx_buffer_h args_buffer = nullptr;

static void cleanup() {
  if (device) {
    vx_mem_free(A_buffer);
    vx_mem_free(B0_buffer);
    vx_mem_free(B1_buffer);
    vx_mem_free(C_buffer);
    vx_mem_free(krnl_buffer);
    vx_mem_free(args_buffer);
    vx_dev_close(device);
  }
}

#define RT_CHECK(_expr)                                       \
  do {                                                        \
    int _ret = _expr;                                         \
    if (0 == _ret)                                            \
      break;                                                  \
    printf("Error: '%s' returned %d!\n", #_expr, (int)_ret);  \
    cleanup();                                                \
    return -1;                                                \
  } while (false)

int main() {
  constexpr uint32_t M = cfg::tileM;
  constexpr uint32_t N = cfg::tileN;
  constexpr uint32_t K = cfg::tileK;
  constexpr uint32_t kElemsPerCase = slot_elem_count();

  static_assert(cfg::tileN == cfg::tileK,
                "wmma_vv_overlap expects tileN == tileK for the chained dependency case");
  static_assert(cfg::tileK <= cfg::tileM,
                "wmma_vv_overlap expects tileK <= tileM for dst-eq-srcB-twice");

  const uint32_t case_mask = enabled_case_mask();

  std::vector<float> A(M * K);
  std::vector<float> B0(K * N);
  std::vector<float> B1(K * N);
  std::vector<float> C(kCaseCount * kElemsPerCase, 0.0f);
  std::vector<float> ref_unique;
  std::vector<float> ref_a_twice;
  std::vector<float> ref_b_twice;

  for (uint32_t i = 0; i < A.size(); ++i)
    A[i] = pattern_a(i);
  for (uint32_t i = 0; i < B0.size(); ++i)
    B0[i] = pattern_b0(i);
  for (uint32_t i = 0; i < B1.size(); ++i)
    B1[i] = pattern_b1(i);

  ref_unique = matmul(A, B0, M, N, K);
  ref_a_twice = matmul(ref_unique, B1, M, N, K);
  ref_b_twice = matmul(A, take_top_rows(ref_unique, K, N), M, N, K);

  kernel_arg_t kernel_arg{};
  kernel_arg.case_mask = case_mask;

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

  RT_CHECK(vx_mem_alloc(device, A.size() * sizeof(float), VX_MEM_READ, &A_buffer));
  RT_CHECK(vx_mem_address(A_buffer, &kernel_arg.A_addr));
  RT_CHECK(vx_mem_alloc(device, B0.size() * sizeof(float), VX_MEM_READ, &B0_buffer));
  RT_CHECK(vx_mem_address(B0_buffer, &kernel_arg.B0_addr));
  RT_CHECK(vx_mem_alloc(device, B1.size() * sizeof(float), VX_MEM_READ, &B1_buffer));
  RT_CHECK(vx_mem_address(B1_buffer, &kernel_arg.B1_addr));
  RT_CHECK(vx_mem_alloc(device, C.size() * sizeof(float), VX_MEM_WRITE, &C_buffer));
  RT_CHECK(vx_mem_address(C_buffer, &kernel_arg.C_addr));

  RT_CHECK(vx_copy_to_dev(A_buffer, A.data(), 0, A.size() * sizeof(float)));
  RT_CHECK(vx_copy_to_dev(B0_buffer, B0.data(), 0, B0.size() * sizeof(float)));
  RT_CHECK(vx_copy_to_dev(B1_buffer, B1.data(), 0, B1.size() * sizeof(float)));
  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));
  RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));

  uint32_t grid_dim[] = {1};
  uint32_t block_dim[] = {1};
  RT_CHECK(vx_start_g(device, krnl_buffer, args_buffer, 1, grid_dim, block_dim, 0));
  RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));
  RT_CHECK(vx_copy_from_dev(C.data(), C_buffer, 0, C.size() * sizeof(float)));

  int errors = 0;
  uint32_t cases = 0;
  for (uint32_t bit : {kCaseUniqueDst, kCaseDstEqSrcA, kCaseDstEqSrcB,
                       kCaseDstEqSrcATwice, kCaseDstEqSrcBTwice}) {
    if ((case_mask & bit) == 0)
      continue;

    ++cases;
    const uint32_t slot = slot_index(bit);
    const float* expected =
        (bit == kCaseDstEqSrcATwice) ? ref_a_twice.data()
      : (bit == kCaseDstEqSrcBTwice) ? ref_b_twice.data()
      : ref_unique.data();
    const float* actual = C.data() + slot * kElemsPerCase;

    for (uint32_t i = 0; i < kElemsPerCase; ++i) {
      if (std::fabs(actual[i] - expected[i]) > 1e-4f) {
        if (errors < 10) {
          printf("*** error: case=%s elem=%u expected=%f, actual=%f\n",
                 case_name(bit), i, expected[i], actual[i]);
        }
        ++errors;
      }
    }
  }

  cleanup();
  if (errors != 0) {
    std::cout << "Found " << errors << " errors!" << std::endl;
    return 1;
  }

  std::cout << "PASSED! cases=" << cases << std::endl;
  return 0;
}

#include <iostream>
#include <unistd.h>
#include <string.h>
#include <vector>
#include <chrono>
#include <vortex.h>
#include "common.h"
#include "VX_types.h"

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
  static int generate() { return rand() % 100; }
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
    if (std::abs(fa.i - fb.i) > 6) {
      if (errors < 100)
        printf("*** error: [%d] expected=%f, actual=%f\n", index, b, a);
      return false;
    }
    return true;
  }
};

///////////////////////////////////////////////////////////////////////////////

const char* kernel_file = "kernel.vxbin";
uint32_t size = 8;
uint32_t vec_len_per_thread = 4;

vx_device_h device      = nullptr;
vx_buffer_h wall_buffer = nullptr;  // full wall (num_rows x num_cols), read-only
vx_buffer_h src_buffer  = nullptr;  // previous accumulated costs
vx_buffer_h dst_buffer  = nullptr;  // output of this step
vx_buffer_h krnl_buffer = nullptr;
vx_buffer_h args_buffer = nullptr;
kernel_arg_t kernel_arg = {};

static void show_usage() {
  std::cout << "Vortex Pathfinder_V Test." << std::endl;
  std::cout << "Usage: [-k kernel] [-v vec_len_per_thread] [-n size] [-h help]" << std::endl;
}

static void parse_args(int argc, char **argv) {
  int c;
  while ((c = getopt(argc, argv, "n:v:k:h")) != -1) {
    switch (c) {
    case 'n': size = atoi(optarg); break;
    case 'v': vec_len_per_thread = atoi(optarg); break;
    case 'k': kernel_file = optarg; break;
    case 'h': show_usage(); exit(0);
    default:  show_usage(); exit(-1);
    }
  }
}

void cleanup() {
  if (device) {
    vx_mem_free(wall_buffer);
    vx_mem_free(src_buffer);
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
  uint32_t num_points = num_cols * num_rows;

  uint32_t wall_buf_size = num_points * sizeof(TYPE);
  uint32_t row_buf_size  = num_cols   * sizeof(TYPE);

  std::cout << "data type: "          << Comparator<TYPE>::type_str() << std::endl;
  std::cout << "number of points: "   << num_points                   << std::endl;
  std::cout << "vec_len_per_thread: " << vec_len_per_thread           << std::endl;

  kernel_arg.num_cols           = num_cols;
  kernel_arg.num_rows           = num_rows;
  kernel_arg.num_points         = num_points;
  kernel_arg.vec_len_per_thread = vec_len_per_thread;

  // One logical thread per RVV chunk, block_dim=1
  uint32_t num_threads  = num_cols / vec_len_per_thread;
  uint32_t grid_dim[1]  = { num_threads };
  uint32_t block_dim[1] = { 1 };

  // allocate device memory
  std::cout << "allocate device memory" << std::endl;
  uint64_t wall_base_addr;
  RT_CHECK(vx_mem_alloc(device, wall_buf_size, VX_MEM_READ,       &wall_buffer));
  RT_CHECK(vx_mem_address(wall_buffer, &wall_base_addr));
  RT_CHECK(vx_mem_alloc(device, row_buf_size,  VX_MEM_READ_WRITE, &src_buffer));
  RT_CHECK(vx_mem_address(src_buffer,  &kernel_arg.src1_addr));
  RT_CHECK(vx_mem_alloc(device, row_buf_size,  VX_MEM_READ_WRITE, &dst_buffer));
  RT_CHECK(vx_mem_address(dst_buffer,  &kernel_arg.dst_addr));

  std::cout << "wall_base=0x" << std::hex << wall_base_addr          << std::endl;
  std::cout << "src_addr=0x"  << std::hex << kernel_arg.src1_addr    << std::endl;
  std::cout << "dst_addr=0x"  << std::hex << kernel_arg.dst_addr     << std::endl;

  // generate and upload full wall once
  std::vector<TYPE> h_wall(num_points);
  for (uint32_t i = 0; i < num_points; ++i)
    h_wall[i] = Comparator<TYPE>::generate();

  std::cout << "upload wall buffer" << std::endl;
  RT_CHECK(vx_copy_to_dev(wall_buffer, h_wall.data(), 0, wall_buf_size));

  // seed src with row 0
  std::vector<TYPE> h_src(num_cols);
  std::vector<TYPE> h_dst(num_cols);
  for (uint32_t i = 0; i < num_cols; ++i) h_src[i] = h_wall[i];
  RT_CHECK(vx_copy_to_dev(src_buffer, h_src.data(), 0, row_buf_size));

  // upload kernel binary once
  std::cout << "upload kernel binary" << std::endl;
  RT_CHECK(vx_upload_kernel_file(device, kernel_file, &krnl_buffer));

  uint64_t total_cycles_per_core(0);
  uint64_t total_instrs_per_core(0);
  uint64_t cycles_per_core;
  uint64_t instrs_per_core;

  auto time_start = std::chrono::high_resolution_clock::now();

  for (uint32_t k = 0; k < num_rows - 1; ++k) {

    // point src0_addr at row k+1 of the wall (already on device)
    kernel_arg.src0_addr = wall_base_addr + (uint64_t)(k + 1) * row_buf_size;

    // re-upload kernel args with updated src0_addr
    RT_CHECK(vx_upload_bytes(device, &kernel_arg, sizeof(kernel_arg_t), &args_buffer));

    // launch
    RT_CHECK(vx_start_g(device, krnl_buffer, args_buffer, 1, grid_dim, block_dim, 0));
    RT_CHECK(vx_ready_wait(device, VX_MAX_TIMEOUT));

    // collect perf counters
    RT_CHECK(vx_mpm_query(device, 0, VX_CSR_MCYCLE,   0, &cycles_per_core));
    RT_CHECK(vx_mpm_query(device, 0, VX_CSR_MINSTRET, 0, &instrs_per_core));
    total_cycles_per_core += cycles_per_core;
    total_instrs_per_core += instrs_per_core;

    // download result
    RT_CHECK(vx_copy_from_dev(h_dst.data(), dst_buffer, 0, row_buf_size));

    // feed result back as src for next iteration
    for (uint32_t i = 0; i < num_cols; ++i) h_src[i] = h_dst[i];
    RT_CHECK(vx_copy_to_dev(src_buffer, h_src.data(), 0, row_buf_size));
  }

  auto time_end = std::chrono::high_resolution_clock::now();
  double elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
      time_end - time_start).count();
  printf("Elapsed time: %lg ms\n", elapsed);
  printf("total_cycles=%ld total_instrs=%ld\n", total_cycles_per_core, total_instrs_per_core);

  // verify against CPU reference
  std::cout << "verify result" << std::endl;
  std::vector<int> result_s(num_cols, 0);
  std::vector<int> src_ref(num_cols, 0);
  std::vector<TYPE> wall_check(h_wall);
  int* result = run(wall_check.data(), result_s.data(), src_ref.data(),
                    num_cols, num_rows, 1);

  int errors = 0;
  for (uint32_t i = 0; i < num_cols; ++i) {
    if (!Comparator<TYPE>::compare(h_dst[i], result[i], i, errors))
      ++errors;
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

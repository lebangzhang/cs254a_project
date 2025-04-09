#pragma once

#include "arch.h"
#include "instr.h"
#include "instr_trace.h"
#include <simobject.h>
#include "types.h"

namespace vortex {

class Core;

class VecUnit : public SimObject<VecUnit> {
public:
  struct TraceData : public ITraceData {
    using Ptr = std::shared_ptr<TraceData>;
    std::vector<std::vector<mem_addr_size_t>> mem_addrs;
    TraceData(uint32_t num_lanes) : mem_addrs(num_lanes) {
        for (uint32_t i = 0; i < num_lanes; ++i) {
            mem_addrs.at(i).reserve(4);
        }
    }
  };

  struct ExeRet {
    VpuType vpu_type;
    bool rd_write;
  };

  struct PerfStats {
    uint64_t reads;
    uint64_t writes;
    uint64_t latency;
    uint64_t stalls;

    PerfStats()
      : reads(0)
      , writes(0)
      , latency(0)
      , stalls(0)
    {}

    PerfStats& operator+=(const PerfStats& rhs) {
      this->reads   += rhs.reads;
      this->writes  += rhs.writes;
      this->latency += rhs.latency;
      this->stalls  += rhs.stalls;
      return *this;
    }
  };

  std::vector<SimPort<instr_trace_t*>> Inputs;
  std::vector<SimPort<instr_trace_t*>> Outputs;

  VecUnit(const SimContext& ctx,
          const char* name,
          const Arch& arch,
          Core* core);

  ~VecUnit();

  void reset();

  void tick();

  bool get_csr(uint32_t addr, uint32_t wid, uint32_t tid, Word* value);

  bool set_csr(uint32_t addr, uint32_t wid, uint32_t tid, Word value);

  void load(const Instr &instr, uint32_t wid, uint32_t tid, const std::vector<reg_data_t>& rs1_data, const std::vector<reg_data_t>& rs2_data, std::vector<mem_addr_size_t>& mem_addrs);

  void store(const Instr &instr, uint32_t wid, uint32_t tid, const std::vector<reg_data_t>& rs1_data, const std::vector<reg_data_t>& rs2_data, std::vector<mem_addr_size_t>& mem_addrs);

  ExeRet execute(const Instr &instr, uint32_t wid, uint32_t tid, const std::vector<reg_data_t>& rs1_data, const std::vector<reg_data_t>& rs2_data, std::vector<reg_data_t>& rd_data, VpuTraceData::data_t& trace_data);

  const PerfStats& perf_stats() const;

private:

  class Impl;
  Impl* impl_;
};

}

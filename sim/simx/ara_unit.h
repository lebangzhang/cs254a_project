#pragma once

#include "arch.h"
#include "instr.h"
#include "instr_trace.h"
#include <simobject.h>
#include "types.h"
#include "ara_lane_unit.h"
#include "func_unit.h"

namespace vortex {

class Core;

class AraUnit : public FuncUnit {
public:
  AraUnit(const SimContext& ctx, Core* core);

  ~AraUnit();

  void reset();

  void tick();

private:

  std::vector<SimPort<instr_trace_t*>> lane_req_ports;
  std::vector<SimPort<instr_trace_t*>> lane_rsp_ports;
  uint32_t num_ara2_lane_insn = 8;
  std::vector<AraLaneUnit::Ptr> lane_unit_;
  uint32_t ARA_MAX_LANE_INSN;
};

}

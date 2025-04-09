// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "instr_trace.h"
#include "gpr_unit.h"

namespace vortex {

class VOpcUnit : public SimObject<VOpcUnit> {
public:
  SimPort<instr_trace_t *> Input;
  SimPort<instr_trace_t *> Output;

  std::array<SimPort<GprReq>, NUM_SRC_REGS> gpr_req_ports;
  std::array<SimPort<GprRsp>, NUM_SRC_REGS> gpr_rsp_ports;

  std::array<SimPort<GprReq>, NUM_SRC_REGS> vgpr_req_ports;
  std::array<SimPort<GprRsp>, NUM_SRC_REGS> vgpr_rsp_ports;

  VOpcUnit(const SimContext &ctx)
    : SimObject<VOpcUnit>(ctx, "vopc-unit")
    , Input(this, 1)
    , Output(this)
    , gpr_req_ports(make_array<SimPort<GprReq>, NUM_SRC_REGS>(this))
    , gpr_rsp_ports(make_array<SimPort<GprRsp>, NUM_SRC_REGS>(this))
    , vgpr_req_ports(make_array<SimPort<GprReq>, NUM_SRC_REGS>(this))
    , vgpr_rsp_ports(make_array<SimPort<GprRsp>, NUM_SRC_REGS>(this)) {
    this->reset();
  }

  virtual ~VOpcUnit() {}

  virtual void reset() {
    pending_rsps_ = 0;
    total_stalls_ = 0;
  }

  virtual void tick() {
    // process incoming instructions
    if (Input.empty())
      return;
    auto trace = Input.front();

    if (0 == pending_rsps_) {
      // calculate operands to fetch
      std::bitset<NUM_SRC_REGS> opd_to_fetch;
      for (uint32_t i = 0; i < NUM_SRC_REGS; i++) {
        if ((trace->src_regs[i].type != RegType::None)
         && !(trace->src_regs[i].idx == 0 && trace->src_regs[i].type == RegType::Integer)) {
          // skip duplicates
          bool is_dup = false;
          for (uint32_t j = 0; j < i; j++) {
            if (trace->src_regs[i].idx == trace->src_regs[j].idx) {
              is_dup = true;
              break;
            }
          }
          if (!is_dup) {
            opd_to_fetch.set(i);
            ++pending_rsps_;
          }
        }
      }

      // Send GPR requests
      for (uint32_t i = 0; i < NUM_SRC_REGS; i++) {
        if (opd_to_fetch.test(i)) {
          GprReq gpr_req;
          gpr_req.rid = trace->src_regs[i].idx;
          gpr_req.wid = trace->wid;
          gpr_req.opd = i;
          gpr_req_ports.at(i).push(gpr_req);
        }
      }
    }

    // process incoming GPR responses
    for (uint32_t i = 0; i < NUM_SRC_REGS; i++) {
      if (gpr_rsp_ports.at(i).empty())
        continue;
      assert(pending_rsps_ != 0);
      --pending_rsps_;
      auto rsp = gpr_rsp_ports.at(i).front();
      __unused(rsp);
      gpr_rsp_ports.at(i).pop();
    }

    // process outgoing instructions
    if (0 == pending_rsps_) {
      auto trace = Input.front();
      this->Output.push(trace);
      Input.pop();
    }
  }

  uint32_t total_stalls() const {
    return total_stalls_;
  }

private:
  uint32_t pending_rsps_ = 0;
  uint32_t total_stalls_ = 0;
};

} // namespace vortex

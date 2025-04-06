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

/* Standard OPC Units */
class OpcUnit : public SimObject<OpcUnit> {
public:
  typedef GprUnit<NUM_OPCS, NUM_GPR_BANKS> GPR;

  SimPort<instr_trace_t *> Input;
  SimPort<instr_trace_t *> Output;

  SimPort<GPR::Req> gpr_req_port;
  SimPort<GPR::Rsp> gpr_rsp_port;

  OpcUnit(const SimContext &ctx)
    : SimObject<OpcUnit>(ctx, "OpcUnit")
    , Input(this)
    , Output(this)
    , gpr_req_port(this)
    , gpr_rsp_port(this) {
    this->reset();
  }

  virtual ~OpcUnit() {}

  virtual void reset() {
    opd_to_fetch_.reset();
    pending_rsp_ = 0;
    total_stalls_ = 0;
  }

  virtual void tick() {
    // process outgoing instructions
    {
      //--
    }

    // process incoming instructions
    if (Input.empty())
      return;
    auto trace = Input.front();
    if (!opd_to_fetch_.any()) {
      // calculate operands to fetch
      for (int i = 0; i < NUM_SRC_REGS; i++) {
        if ((trace->src_regs[i].type != RegType::None)
         && (trace->src_regs[i].idx == 0 && trace->src_regs[i].type == RegType::Integer)) {
          opd_to_fetch_. set(i);
          ++pending_rsp_;
        }
      }
    }

    // Send to GPR
    for (int i = 0; i < NUM_SRC_REGS; i++) {

      if (opd_to_fetch[i] != 0) {

        RegReq opd_request;
        opd_request.src_reg_idx = opd_to_fetch[i];

        /*if(gpr_req_port.empty()){*/
        gpr_req_port.push(opd_request, 1);
        opd_to_fetch[i] = 0;
        /*}*/

        pending_rsp += 1;
      }
    }

    // Once all Opds sent out
    if (pending_rsp == 0) {
      Input.pop();
      /*Output.push(trace, 1);*/
      locked = false;
    }
  }

  uint32_t total_stalls() const {
    return total_stalls_;
  }

private:
  std::bitset<NUM_SRC_REGS> opd_to_fetch_;
  uint32_t pending_rsp_ = 0;
  uint32_t total_stalls_ = 0;
};

} // namespace vortex

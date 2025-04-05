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

private:
  uint32_t total_stalls_ = 0;
  bool locked = false;
  uint32_t pending_rsp = 0;

public:
  SimPort<instr_trace_t *> Input;
  SimPort<instr_trace_t *> Output;

  SimPort<RegReq> gpr_req_port;
  SimPort<RegRsp> gpr_rsp_port;

  OpcUnit(const SimContext &ctx)
      : SimObject<OpcUnit>(ctx, "Standard OPC Unit"), Input(this), Output(this), gpr_req_port(this), gpr_rsp_port(this) {
    total_stalls_ = 0;
  }

  virtual ~OpcUnit() {}

  virtual void reset() {
    total_stalls_ = 0;
  }

  virtual void tick() {

    total_stalls_ += 1;

    if (Input.empty())
      return;

    auto trace = Input.front();

    uint32_t stalls = 0;

    // Get Number of opd to fetch only once
    int opd_to_fetch[3] = {0, 0, 0};
    if (!locked) {
      for (int i = 0; i < NUM_SRC_REGS; i++) {
        if ((trace->src_regs[i].type != RegType::None) && ((trace->src_regs[i].idx != 0) && (trace->src_regs[i].type == RegType::Integer))) {
          opd_to_fetch[i] = trace->src_regs[i].idx;
        }
      }
      locked = true;
      pending_rsp = 0;
    }

    // Handle GPR response
    if (!gpr_rsp_port.empty()) {
      gpr_rsp_port.pop();
      pending_rsp -= 1;
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
};

} // namespace vortex

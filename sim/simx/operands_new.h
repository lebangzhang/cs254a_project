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
#include "opc_unit.h"
#include "gpr_unit.h"

namespace vortex {

class Operands : public SimObject<Operands> {

private:
  uint32_t total_stalls_ = 0;
  uint32_t round_robin_counter = 0;

public:
  SimPort<instr_trace_t *> Input;
  SimPort<instr_trace_t *> Output;
  std::vector<OpcUnit::Ptr> opc_units_;

  Operands(const SimContext &ctx)
      : SimObject<Operands>(ctx, "Operands")
      , Input(this)
      , Output(this)
      , opc_units_(NUM_OPCS) {
    total_stalls_ = 0;

    // Instantiate Opc Units
    for (uint32_t i = 0; i < NUM_OPCS; i++) {
      opc_units_.at(i) = OpcUnit::Create();
    }

    // Instantiate Reg File
    auto gpr_unit = GprUnit::Create();

    // Connect Opc to GprUnit
    for (uint32_t i = 0; i < NUM_OPCS; i++) {
      opc_units_.at(i)->gpr_req_port.bind(&gpr_unit->ReqIn.at(i));
    }

    for (uint32_t i = 0; i < NUM_OPCS; i++) {
      opc_units_.at(i)->gpr_rsp_port.bind(&gpr_unit->ReqOut.at(i));
    }
  }

  virtual ~Operands() {}

  virtual void reset() {
    total_stalls_ = 0;
  }

  virtual void tick() {

    if (Input.empty())
      return;

    auto trace = Input.front();
    uint32_t stalls = 0;

    // Find free OpcUnit
    for (uint32_t i = 0; i < NUM_OPCS; i++) {

      if (opc_units_.at(i)->Input.empty()) {
        opc_units_.at(i)->Input.push(trace, 1);
        Input.pop();
        break;
      }
    }
    /*total_stalls_ += stalls;*/

    // Round Robin to search for output
    for (uint32_t i = 0; i < NUM_OPCS; i++) {

      uint32_t counter = (round_robin_counter + i) % NUM_OPCS;

      if (!opc_units_.at(counter)->Output.empty()) {
        Output.push(trace, 1);

        round_robin_counter += 1;
        opc_units_.at(counter)->Output.pop();
        break;
      }
    }

    /*Output.push(trace, 2);*/

    // TO FIX : The Total Stalls
    /*
    total_stalls_ += stalls;
Output.push(trace, 2 + stalls);
DT(3, "pipeline-operands: " << *trace);
    */
  }

  uint32_t total_stalls() const {
    return total_stalls_;
  }
};

} // namespace vortex

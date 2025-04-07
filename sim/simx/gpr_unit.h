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

namespace vortex {

template <uint32_t NUM_REQS, uint32_t NUM_BANKS>
class GprUnit : public SimObject<GprUnit<NUM_REQS, NUM_BANKS>> {
public:
  struct Req {
    uint32_t opd;
    uint32_t rid;
    uint32_t wid;
  };

  struct Rsp {
    uint32_t opd;
  };

  std::array<SimPort<Req>, NUM_REQS> ReqIn;
  std::array<SimPort<Rsp>, NUM_REQS> RspOut;

  GprUnit(const SimContext &ctx)
    : SimObject<GprUnit<NUM_REQS, NUM_BANKS>>(ctx, "GprUnit")
    , ReqIn(make_array<SimPort<Req>, NUM_REQS>(this, 2))
    , RspOut(make_array<SimPort<Rsp>, NUM_REQS>(this)) {
    total_stalls_ = 0;
  }

  virtual ~GprUnit() {}

  virtual void reset() {
    total_stalls_ = 0;
  }

  virtual void tick() {
    if (ReqIn.empty())
      return;
    for (uint32_t b = 0; b < NUM_BANKS; b++) {
      for (uint32_t r = 0; r < NUM_REQS; r++) {
        if (!ReqIn.at(r).empty()) {
          auto& req = ReqIn.at(r).front();
          uint32_t bank_id = get_bank_id(req);
          if ((bank_id == b)) {
            Rsp rsp;
            rsp.opd = req.opd;
            RspOut.at(r).push(rsp);
            ReqIn.at(r).pop();
            break;
          }
        }
      }
    }
  }

  uint32_t total_stalls() const {
    return total_stalls_;
  }

private:
  uint32_t total_stalls_ = 0;

  uint32_t get_bank_id(const Req& req) const {
    return req.rid % NUM_BANKS;
  }
};

typedef GprUnit<NUM_OPCS * NUM_SRC_REGS, NUM_GPR_BANKS> GPR;

} // namespace vortex

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
    uint32_t wis;
  };

  struct Rsp {
    uint32_t opd;
  };

  std::vector<SimPort<Req>> ReqIn;
  std::vector<SimPort<Rsp>> ReqOut;

  GprUnit(const SimContext &ctx)
      : SimObject<GprUnit<NUM_REQS, NUM_BANKS>>(ctx, "GprUnit")
      , ReqIn(NUM_REQS, this)
      , ReqOut(NUM_REQS, this) {
    total_stalls_ = 0;
  }

  virtual ~GprUnit() {}

  virtual void reset() {
    total_stalls_ = 0;
  }

  virtual void tick() {
    if (ReqIn.empty())
      return;
    for (uint32_t i = 0; i < NUM_BANKS; i++) {
      for (uint32_t j = 0; j < NUM_REQS; j++) {
        if (!ReqIn.at(i).empty()) {
          auto& req = ReqIn.at(i).front();
          uint32_t bank_id = get_bank_id(req);
          if ((bank_id == i)) {
            Rsp rsp;
            rsp.opd = req.opd;
            ReqOut.at(i).push(rsp, 1);
            ReqIn.at(i).pop();
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

} // namespace vortex

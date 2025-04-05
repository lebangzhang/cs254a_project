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

/* Requests to Reg File */
struct RegReq {

  uint32_t src_reg_idx;

  RegReq(uint32_t _src_reg_idx = 0) : src_reg_idx(_src_reg_idx) {}
};

// Probably don't need this
// TO FIX : Possible Remove this
/* Response from Reg File */
struct RegRsp {

  uint32_t valid;

  RegRsp(uint32_t _valid = 0) : valid(_valid) {}
};

class GprUnit : public SimObject<GprUnit> {

private:
  static constexpr uint32_t NUM_BANKS = 4;

  // Maybe need to make public ???
  static constexpr uint32_t NUM_UNITS = 4;

  uint32_t total_stalls_ = 0;

public:
  std::vector<SimPort<RegReq>> ReqIn;
  std::vector<SimPort<RegRsp>> ReqOut;

  GprUnit(const SimContext &ctx)
      : SimObject<GprUnit>(ctx, "Register File"), ReqIn(NUM_UNITS, this), ReqOut(NUM_UNITS, this) {
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

      for (uint32_t j = 0; j < NUM_UNITS; j++) {

        if (!ReqIn.at(i).empty()) {

          auto request = ReqIn.at(i).front();

          uint32_t bank = request.src_reg_idx % NUM_BANKS;

          if ((bank == i)) {

            RegRsp response;
            ReqOut.at(i).push(response, 1);
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
};

} // namespace vortex

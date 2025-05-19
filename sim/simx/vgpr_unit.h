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

struct VgprReq {
  uint32_t opd;
  uint32_t rid;
  uint32_t wid;

  friend std::ostream& operator<<(std::ostream& os, const VgprReq& req) {
    os << "opd=" << req.opd << ", rid=" << req.rid << ", wid=" << req.wid;
    return os;
  }
};

struct VgprRsp {
  uint32_t opd;

  friend std::ostream& operator<<(std::ostream& os, const VgprRsp& rsp) {
    os << "opd=" << rsp.opd;
    return os;
  }
};

template <uint32_t NUM_REQS, uint32_t NUM_BANKS>
class VgprUnit : public SimObject<VgprUnit<NUM_REQS, NUM_BANKS>> {
public:

  using ReqXbar = TxCrossBar<VgprReq>;

  std::array<SimPort<VgprReq>, NUM_REQS> ReqIn;
  std::array<SimPort<VgprRsp>, NUM_REQS> RspOut;

  VgprUnit(const SimContext &ctx)
    : SimObject<VgprUnit<NUM_REQS, NUM_BANKS>>(ctx, "vgpr-unit")
    , ReqIn(make_array<SimPort<VgprReq>, NUM_REQS>(this))
    , RspOut(make_array<SimPort<VgprRsp>, NUM_REQS>(this)) {
    char sname[100];
		snprintf(sname, 100, "%s-xbar", this->name().c_str());
    crossbar_ = ReqXbar::Create(sname, NUM_REQS, NUM_BANKS,
		 [](const VgprReq& req)->uint32_t {
      //uint32_t bank_id = ((req.wid & BANKID_WIS_MASK) << BANKID_REG_BITS) | (req.rid & BANKID_REG_MASK);
			return req.rid % NUM_BANKS;
		});
    for (uint32_t i = 0; i < NUM_REQS; ++i) {
			ReqIn.at(i).bind(&crossbar_->Inputs.at(i));
		}
  }

  virtual ~VgprUnit() {}

  virtual void reset() {
    //--
  }

  virtual void tick() {
    if (ReqIn.empty())
      return;

    // Serving operand requests for each clock cycle
    for (uint32_t b = 0; b < NUM_BANKS; b++) {
      auto& output = crossbar_->Outputs.at(b);
      if (output.empty())
        continue;
      auto& req = output.front();
      VgprRsp rsp;
      rsp.opd = req.data.opd;
      RspOut.at(req.input).push(rsp);
      output.pop();
    }
  }

  uint32_t total_stalls() const {
    return crossbar_->collisions();
  }

private:
  typename ReqXbar::Ptr crossbar_;
  constexpr static uint32_t BANK_SEL_BITS    = log2ceil(NUM_BANKS);
  constexpr static uint32_t _BANKID_WIS_BITS = std::min<uint32_t>(ISSUE_WIS_BITS, BANK_SEL_BITS - (BANK_SEL_BITS / 2));
  constexpr static uint32_t BANKID_WIS_BITS  = (BANK_SEL_BITS > 1 && ISSUE_WIS_BITS != 0) ? _BANKID_WIS_BITS : 0;
  constexpr static uint32_t BANKID_REG_BITS  = BANK_SEL_BITS - BANKID_WIS_BITS;
  constexpr static uint32_t BANKID_REG_MASK  = (1 << BANKID_REG_BITS) - 1;
  constexpr static uint32_t BANKID_WIS_MASK  = (1 << BANKID_WIS_BITS) - 1;
};

// TOFIX : Need to fix this 

/*#if defined(EXT_V_ENABLE) || defined(EXT_ARA2_ENABLE)*/
typedef VgprUnit<NUM_OPCS, NUM_VGPR_BANKS> VGPR;
/*#else*/
/*typedef GprUnit<NUM_OPCS, NUM_GPR_BANKS> GPR;*/
/*#endif*/


}

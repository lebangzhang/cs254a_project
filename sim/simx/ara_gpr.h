// Copyright © 2019-2023
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "instr_trace.h"

namespace vortex {

struct AraGprReq {
  uint32_t port_id;
  uint32_t rid;
  uint32_t wid;
};




class Ara_Gpr : public SimObject<Ara_Gpr> {

private: 
		uint32_t total_stalls_ = 0;
        uint32_t num_ara2_lane_insn = 10;

public:

    // Take in the inputs 
    SimPort<instr_trace_t*> Input;
    SimPort<instr_trace_t*> Output;

    SimPort<instr_trace_t*> ara_gpr_req;
    SimPort<instr_trace_t*> ara_gpr_rsp;


    Ara_Gpr(const SimContext& ctx)
			: SimObject<Ara_Gpr>(ctx, "unit")
			, Input(this)
			, Output(this)
            , ara_gpr_req(this)
            , ara_gpr_rsp(this)

    {
		total_stalls_ = 0;
	}

    virtual ~Ara_Gpr() {}

    virtual void reset() {
		total_stalls_ = 0;
	}

    virtual void tick() {

        // 1. Check if request port is empty 
        /*
        for(int i = 0; i < num_ara2_lane_insn; i++){

            // If none of the ports are empty + Not yet been processed ==> Send requests to VGPR
            if(!this->op_req_port.at(i).empty() && (bitvector.at(i) == 0)){

                printf(" XXXXX-11111 %d %d\n", i, this->op_req_port.at(i).size());
                auto trace = this->op_req_port.at(i).front();
                this->op_rsp_port.at(i).push(trace, 1);
                this->op_req_port.at(i).pop();
                printf(" XXXXX-22222 %d %d\n", i, this->op_req_port.at(i).size());
            }

        }
        */
    };

	bool writeback(instr_trace_t* trace) {
		__unused(trace);
		return true;
	}

	uint32_t total_stalls() const {
		return total_stalls_;
	}
};

}

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

struct AraGprPkt {
  uint32_t port_id;
  uint32_t rid;

  friend std::ostream& operator<<(std::ostream& os, const AraGprPkt& req) {
    os << "123123";
    return os;
  }

};




class Ara_Gpr : public SimObject<Ara_Gpr> {

private: 
	uint32_t total_stalls_ = 0;
    uint32_t NUM_ARA_GPR_BANKS = 8;
    uint32_t num_gpr_arbitration_port = 8;

public:

    // Take in the inputs 
    SimPort<instr_trace_t*> Input;
    SimPort<instr_trace_t*> Output;

    std::vector<SimPort<AraGprPkt>> ara_gpr_req_port;
    std::vector<SimPort<AraGprPkt>> ara_gpr_rsp_port;


    Ara_Gpr(const SimContext& ctx)
			: SimObject<Ara_Gpr>(ctx, "unit")
			, Input(this)
			, Output(this)
            , ara_gpr_req_port(num_gpr_arbitration_port, this)
            , ara_gpr_rsp_port(num_gpr_arbitration_port, this)


    {
		total_stalls_ = 0;
	}

    virtual ~Ara_Gpr() {}

    virtual void reset() {
		total_stalls_ = 0;
	}

    virtual void tick() {


        // TOFIX : Simulate bank conflicts
        // 1. Simulate bank conflicts
        for(int i = 0; i < num_gpr_arbitration_port; i++){ 

            // 1a. Check if request port is empty 
            if(!ara_gpr_req_port.at(i).empty()) {

                // Send Response back 
                AraGprPkt gpr_rsp = ara_gpr_req_port.at(i).front();
                ara_gpr_rsp_port.at(i).push(gpr_rsp, 1);
            
                // Pop from request port
                printf("22222\n");
                ara_gpr_req_port.at(i).pop();
            }
        }
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

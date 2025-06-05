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
#include "ara_gpr.h"

namespace vortex {

// TOFIX : Rename to ARA_Operand_Requestor 
class Operand_Requestor : public SimObject<Operand_Requestor> {

private: 
		uint32_t total_stalls_ = 0;

        // TOFIX : Get this value from lane_unit
        uint32_t num_ara2_lane_insn = 8;

public:

    // Take in the inputs 
    SimPort<instr_trace_t*> Input;
    SimPort<instr_trace_t*> Output;

    std::vector<SimPort<instr_trace_t*>> op_req_port;
    std::vector<SimPort<instr_trace_t*>> op_rsp_port;
    std::vector<int> first_entrance_bitvector;

    Ara_Gpr::Ptr ara_gpr_unit;
    SimPort<instr_trace_t*> ara_gpr_req;
    SimPort<instr_trace_t*> ara_gpr_rsp;


    Operand_Requestor(const SimContext& ctx)
			: SimObject<Operand_Requestor>(ctx, "unit")
			, Input(this)
			, Output(this)
            , op_rsp_port(num_ara2_lane_insn, this)
            , op_req_port(num_ara2_lane_insn, this)
            , first_entrance_bitvector(num_ara2_lane_insn)
            , ara_gpr_req(this)
            , ara_gpr_rsp(this)
    {
		total_stalls_ = 0;

        // Initialize first entrace bitvector 
        for(int i=0; i<num_ara2_lane_insn; i++){
            first_entrance_bitvector.at(i) = 0;
        }

        // Create Ara_Gpr        
        ara_gpr_unit = Ara_Gpr::Create();

        // Bind Ports from operand requestor to ara_gpr
        this->ara_gpr_req.bind(&ara_gpr_unit->ara_gpr_req); 
        ara_gpr_unit->ara_gpr_rsp.bind(&this->ara_gpr_rsp);
	}

    virtual ~Operand_Requestor() {}

    virtual void reset() {
		total_stalls_ = 0;
	}

    virtual void tick() {

        // 2. Handle response from GPR

        // 1. Check if request port is empty 
        for(int i = 0; i < num_ara2_lane_insn; i++){
            // If none of the ports are empty + Not yet been processed ==> Send requests to VGPR
            if(!this->op_req_port.at(i).empty() && (first_entrance_bitvector.at(i) == 0)){
                auto trace = this->op_req_port.at(i).front();
                this->op_rsp_port.at(i).push(trace, 1);
                this->op_req_port.at(i).pop();
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

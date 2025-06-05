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
#include "operand_requestor.h"

namespace vortex {


// TOFIX : Rename to Ara_Lane_Unit
class Lane_Unit : public SimObject<Lane_Unit> {

private: 
		uint32_t total_stalls_ = 0;
        uint32_t num_ara2_lane_insn = 8;

public:

    SimPort<instr_trace_t*> Input;
    SimPort<instr_trace_t*> Output;

    SimPort<instr_trace_t*> lane_req_port;
    SimPort<instr_trace_t*> lane_rsp_port;

    Operand_Requestor::Ptr  op_req_unit;
    std::vector<SimPort<instr_trace_t*>> op_req_port;
    std::vector<SimPort<instr_trace_t*>> op_rsp_port;



    Lane_Unit(const SimContext& ctx)
			: SimObject<Lane_Unit>(ctx, "lane_unit")
			, Input(this)
			, Output(this)
            , lane_rsp_port(this)
            , lane_req_port(this)
            , op_rsp_port(num_ara2_lane_insn, this)
            , op_req_port(num_ara2_lane_insn, this)
    {
		total_stalls_ = 0;
    
        // Create Operand Requestor 
        op_req_unit = Operand_Requestor::Create();

        // Bind Ports to operand requestor inside the lanes
        for(int i=0; i < num_ara2_lane_insn; i++){
            this->op_req_port.at(i).bind(&op_req_unit->op_req_port.at(i)); 
            op_req_unit->op_rsp_port.at(i).bind(&this->op_rsp_port.at(i));
        }
	}

    virtual ~Lane_Unit() {}

    virtual void reset() {
		total_stalls_ = 0;
	}

    virtual void tick() {


        // 3. Handle the output from operand requestor 
        for(int i=0; i < num_ara2_lane_insn; i++){

            auto &op_response = this->op_rsp_port.at(i);

            // Non empty response --> return that trace back to ara_unit
            // TOFIX : Add the concept of ALU and MUL latency 
            if(!op_response.empty()){
                printf("LANE_UNIT-RSP lane  1: req=%d rsp=%d\n", this->lane_req_port.size(), this->lane_rsp_port.size());
                printf("LANE_UNIT-RSP opreq 1: req=%d rsp=%d\n", this->op_req_port.at(i).size(), this->op_rsp_port.at(i).size());
                auto &trace_received = this->op_rsp_port.at(i).front();
		        lane_rsp_port.push(trace_received, 1);
                this->op_rsp_port.at(i).pop();
            }
        }

        // 1. If request port empty ==> Return 
        if (lane_req_port.empty())
			return;

        // 2. If not empty
        for(int i=0; i < num_ara2_lane_insn; i++){
            // Check for empty port ==> Forward request to operand requestor and return from function
            if(this->op_req_port.at(i).empty()){
                printf("LANE_UNIT-REQ lane  1: req=%d rsp=%d\n", this->lane_req_port.size(), this->lane_rsp_port.size());
                printf("LANE_UNIT-REQ opreq 1: req=%d rsp=%d\n", this->op_req_port.at(0).size(), this->op_rsp_port.at(0).size());
		        auto trace = lane_req_port.front();
                this->op_req_port.at(i).push(trace, 1);
		        lane_req_port.pop();
                printf("LANE_UNIT-REQ lane  2: req=%d rsp=%d\n", this->lane_req_port.size(), this->lane_rsp_port.size());
                printf("LANE_UNIT-REQ opreq 2: req=%d rsp=%d\n", this->op_req_port.at(0).size(), this->op_rsp_port.at(0).size());
                return;
            }
        }
        
        /*
        switch (trace->vpu_type) {
            case VpuType::VSET:
            break;
            case VpuType::ARITH:
            case VpuType::ARITH_R:
                delay = 1;
                break;
            case VpuType::IMUL:
                delay = LATENCY_IMUL;
                break;
            case VpuType::IDIV:
                delay = XLEN;
                break;
            case VpuType::FNCP:
            case VpuType::FNCP_R:
                delay = 2;
                break;
            case VpuType::FMA:
            case VpuType::FMA_R:
                delay = LATENCY_FMA;
                break;
            case VpuType::FDIV:
                delay = LATENCY_FDIV;
                break;
            case VpuType::FSQRT:
                delay = LATENCY_FSQRT;
                break;
            case VpuType::FCVT:
                delay = LATENCY_FCVT;
                break;
            default:
                std::abort();
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

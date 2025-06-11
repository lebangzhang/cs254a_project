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
		float active_cycles = 0;
		float total_insn = 0;
		float total_insn_at_once = 0;
        float peak_insn_at_once = 0;

        uint32_t num_ara2_lane_insn = NUM_ARA_LANE_INSN;

public:

    SimPort<instr_trace_t*> Input;
    SimPort<instr_trace_t*> Output;

    // Connection to ara_unit 
    SimPort<instr_trace_t*> lane_req_port;
    SimPort<instr_trace_t*> lane_rsp_port;

    // Connection to operand_requestor
    Operand_Requestor::Ptr  op_req_unit;
    std::vector<SimPort<instr_trace_t*>> op_req_port;
    std::vector<SimPort<instr_trace_t*>> op_rsp_port;

    std::vector<int> microop_counter;

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
        active_cycles = 0;
        total_insn = 0; 
        total_insn_at_once = 0;
        peak_insn_at_once = 0;
    
        // Create Operand Requestor 
        op_req_unit = Operand_Requestor::Create();

        // Bind Ports to operand requestor inside the lanes
        for(int i=0; i < num_ara2_lane_insn; i++){
            this->op_req_port.at(i).bind(&op_req_unit->op_req_port.at(i)); 
            op_req_unit->op_rsp_port.at(i).bind(&this->op_rsp_port.at(i));
        }

        // Initialize microop counter 
        for(int i=0; i < num_ara2_lane_insn; i++){
            microop_counter.push_back(0);
        }
	}

    virtual ~Lane_Unit() {}

    virtual void reset() {
		total_stalls_ = 0;
	}

    virtual void tick() {


        /*DT(3, "----- Entered Lane_Unit ------");*/

        // 2. Handle the output from operand requestor 
        for(int i=0; i < num_ara2_lane_insn; i++){

            auto &op_response = this->op_rsp_port.at(i);

            // Non empty response --> return that trace back to ara_unit
            if(!op_response.empty()){
                auto &trace_received = this->op_rsp_port.at(i).front();
		        lane_rsp_port.push(trace_received, 1);
                this->op_rsp_port.at(i).pop();
            }
        }

        // 0. Calculate total active cycles (can be removed)
        for(int i = 0; i < num_ara2_lane_insn; i++){
            if(!this->op_req_port.at(i).empty()){
                active_cycles += 1;
                break;
            }
        } 

        int temp = 0;
        for(int i = 0; i < num_ara2_lane_insn; i++){
            if(!this->op_req_port.at(i).empty()){
                total_insn_at_once += 1;
                temp += 1;
            }
        } 
        if(temp > peak_insn_at_once){
            peak_insn_at_once = temp; 
        }

        DT(3, "ACTIVE_CYCLES = " << active_cycles);
        DT(3, "TOTAL INSN    = " << total_insn);
        DT(3, "IPC           = " << total_insn         / active_cycles);
        DT(3, "AVG PORT UTIL = " << total_insn_at_once / active_cycles);
        DT(3, "PEAK PORT UTIL= " << peak_insn_at_once); 


        // 1a. If request port empty ==> Return 
        if (lane_req_port.empty()){
            /*DT(3, "----- Finished Lane_Unit ------");*/
			return;
        }

        // 1b. If not empty ==> Take 1 request from ara_unit to 
        for(int i=0; i < num_ara2_lane_insn; i++){

            // Check for empty slot in the op_req_port  ==> Forward request to operand requestor and return from function
            if(this->op_req_port.at(i).empty()){
		        
                // Forward request to operand_requestor
                auto trace = lane_req_port.front();
                this->op_req_port.at(i).push(trace, 1);
                DT(3, "Ara-Lane_Unit: To op_req_port : " << i << " Received request " << *trace);

                // Initialize microop counter 
                uint32_t VL_count = VLEN/XLEN;
                VL_count = VL_count / NUM_ARA_LANES; 
                // TORETURN : For stress testing
                /*VL_count = 4;*/
                // TOFIX : Need to fix method for calculating microop
                microop_counter.at(i) = VL_count;

                // Perf Counter 
                total_insn += 1;

		        lane_req_port.pop();
                return;
            }
        }

        /*DT(3, "----- Finished Lane_Unit ------");*/

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

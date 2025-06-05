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
        
    uint32_t num_gpr_arbitration_port = 8;

public:

    // Take in the inputs 
    SimPort<instr_trace_t*> Input;
    SimPort<instr_trace_t*> Output;

    std::vector<SimPort<instr_trace_t*>> op_req_port;
    std::vector<SimPort<instr_trace_t*>> op_rsp_port;
    std::vector<int> first_entrance_bitvector;

    Ara_Gpr::Ptr ara_gpr_unit;
    std::vector<SimPort<AraGprPkt>> ara_gpr_req_port;
    std::vector<SimPort<AraGprPkt>> ara_gpr_rsp_port;

    std::vector<std::vector<AraGprPkt>> ara_packet_storage; 
    std::vector<int> gpr_packet_pending_counter; 
    std::queue<AraGprPkt> packet_request_fifo;


    Operand_Requestor(const SimContext& ctx)
			: SimObject<Operand_Requestor>(ctx, "unit")
			, Input(this)
			, Output(this)
            , op_rsp_port(num_ara2_lane_insn, this)
            , op_req_port(num_ara2_lane_insn, this)
            , first_entrance_bitvector(num_ara2_lane_insn)
            , ara_gpr_req_port(num_gpr_arbitration_port, this)
            , ara_gpr_rsp_port(num_gpr_arbitration_port, this)
    {
		total_stalls_ = 0;

        // Initialize first entrace bitvector 
        for(int i=0; i < num_ara2_lane_insn; i++){
            first_entrance_bitvector.at(i) = 0;
        }

        // Create Ara_Gpr        
        ara_gpr_unit = Ara_Gpr::Create();

        // Bind Ports from operand requestor to ara_gpr
        for(int i=0; i < num_gpr_arbitration_port; i++){
            this->ara_gpr_req_port.at(i).bind(&ara_gpr_unit->ara_gpr_req_port.at(i)); 
            ara_gpr_unit->ara_gpr_rsp_port.at(i).bind(&this->ara_gpr_rsp_port.at(i));
        }

        // Create + Initialize ara_packet_storage and pending counter
        for(int i=0; i < num_ara2_lane_insn; i++){
            std::vector<AraGprPkt> temp;
            ara_packet_storage.push_back(temp);
            gpr_packet_pending_counter.push_back(0);
        } 
	}

    virtual ~Operand_Requestor() {}

    virtual void reset() {
		total_stalls_ = 0;

        // Reset first entrace bitvector
        for(int i=0; i<num_ara2_lane_insn; i++){
            first_entrance_bitvector.at(i) = 0;
        }

	}

    virtual void tick() {

        // 4. Officially free the port 
        /*
        for(int port_id = 0; port_id < num_ara2_lane_insn; port_id++){

            // This will make sure if port is not empty, we are not taking the old value
            if(first_entrance_bitvector.at(port_id) == 2){
                first_entrance_bitvector.at(port_id) = 0;
            }
        }
        
        // 3. Handle response from GPR
        for(int i = 0; i < num_gpr_arbitration_port; i++){
            
            // Check if response exists
            if(!ara_gpr_rsp_port.at(i).empty()){
                
                // Get port id 
                uint32_t port_id = ara_gpr_rsp_port.at(i).front().port_id;

                // Decrement pending counter for that port_id + pop from response port 
                printf("Ara_packet_pop 1: portid=%d counter=%d \n", port_id, gpr_packet_pending_counter.at(port_id));
                gpr_packet_pending_counter.at(port_id) -= 1;
                printf("Ara_packet_pop 2: portid=%d counter=%d \n", port_id, gpr_packet_pending_counter.at(port_id));
                ara_gpr_rsp_port.at(i).pop();
                

                // Check if all requests have been fufiled
                if(gpr_packet_pending_counter.at(port_id) == 0){

                    // Return trace
                    printf("Return trace\n", port_id, gpr_packet_pending_counter.at(port_id));
                    auto trace = this->op_req_port.at(port_id).front();
                    this->op_rsp_port.at(port_id).push(trace, 1);

                    // Free port 
                    printf("11111\n");
                    this->op_req_port.at(port_id).pop();

                    // This will make sure if port is not empty, we are not taking the old value
                    first_entrance_bitvector.at(port_id) = 2;
                }
            }
        }
        */ 

        // 1. Check if request port is empty 
        for(int port_id = 0; port_id < num_ara2_lane_insn; port_id++){

            // If none of the ports are empty + Not yet been processed ==> Send requests to VGPR
            if(!this->op_req_port.at(port_id).empty() && (first_entrance_bitvector.at(port_id) == 0)){

                // Construct VGPR request, and send it 
                auto trace = this->op_req_port.at(port_id).front();
                
                // Get the operands to fetch 
                std::bitset<NUM_SRC_REGS> opd_to_fetch;

                for (uint32_t i = 0; i < NUM_SRC_REGS; i++) {
                    if (trace->src_regs[i].id() == 0)
                        continue; // skip x0 or empty
                    // skip duplicates
                    bool is_dup = false;
                    for (uint32_t j = 0; j < i; j++) {
                        if (trace->src_regs[i].id() == trace->src_regs[j].id()) {
                            is_dup = true;
                            break;
                        }
                    }
                    if (!is_dup) {
                        opd_to_fetch.set(i);
                    }
                }

                // Construct AraGprPkt requests + Store it 
                std::vector<AraGprPkt> Packet_List;
                for (uint32_t i = 0; i < NUM_SRC_REGS; i++) {
                    if (opd_to_fetch.test(i)) {
                        AraGprPkt gpr_req;
                        gpr_req.rid = trace->src_regs[i].id();
                        gpr_req.port_id = port_id;

                        Packet_List.push_back(gpr_req);

                        // Put it into request fifo 
                        packet_request_fifo.push(gpr_req);
                    }
                }

                // Store the requests + initialize starting number of requests 
                ara_packet_storage.at(port_id) = Packet_List;
                gpr_packet_pending_counter.at(port_id) = ara_packet_storage.at(port_id).size();
                printf("Ara_packet_initalize: %d %d \n", port_id, gpr_packet_pending_counter.at(port_id));


                // Mark finished first pass, but keep it in port to show unfinished request 
                first_entrance_bitvector.at(port_id) = 1;

                // Temporary Fix 
                first_entrance_bitvector.at(port_id) = 0;
                this->op_rsp_port.at(port_id).push(trace, 1);
                this->op_req_port.at(port_id).pop();
            }
        }

        // 2. Move requests from fifo to gpr ports  
        /*
        for(int i=0; i < num_gpr_arbitration_port; i++){

            // 2a. Check if port is free and Check if there are requests 
            if(ara_gpr_req_port.at(i).empty() && !packet_request_fifo.empty()){
                
                AraGprPkt gpr_req = packet_request_fifo.front();


                printf("Ara_gpr_req_sent\n");
                ara_gpr_req_port.at(i).push(gpr_req);
                packet_request_fifo.pop();
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

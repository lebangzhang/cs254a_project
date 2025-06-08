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

class Ara_Operand_Requestor : public SimObject<Ara_Operand_Requestor> {

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
    SimPort<AraGprPkt> ara_gpr_req_ports;
    SimPort<AraGprPkt> ara_gpr_rsp_ports;

    std::vector<std::vector<AraGprPkt>> ara_packet_storage; 
    std::vector<int> gpr_packet_pending_counter; 


    Ara_Operand_Requestor(const SimContext& ctx)
			: SimObject<Ara_Operand_Requestor>(ctx, "unit")
			, Input(this)
			, Output(this)
            , op_rsp_port(num_ara2_lane_insn, this)
            , op_req_port(num_ara2_lane_insn, this)
            , first_entrance_bitvector(num_ara2_lane_insn)
            , ara_gpr_req_ports(this)
            , ara_gpr_rsp_ports(this)
    {
		total_stalls_ = 0;

        // Initialize first entrace bitvector 
        for(uint32_t i=0; i < num_ara2_lane_insn; i++){
            first_entrance_bitvector.at(i) = 0;
        }

        // Create Ara_Gpr        
        ara_gpr_unit = Ara_Gpr::Create();

        // Bind Ports from operand requestor to ara_gpr
        this->ara_gpr_req_ports.bind(&ara_gpr_unit->ara_gpr_req_ports); 
        ara_gpr_unit->ara_gpr_rsp_ports.bind(&this->ara_gpr_rsp_ports);

        // Create + Initialize ara_packet_storage and pending counter
        for(uint32_t i=0; i < num_ara2_lane_insn; i++){
            std::vector<AraGprPkt> temp;
            ara_packet_storage.push_back(temp);
            gpr_packet_pending_counter.push_back(0);
        } 
	}

    virtual ~Ara_Operand_Requestor() {}

    virtual void reset() {
		total_stalls_ = 0;

        // Reset first entrace bitvector
        for(uint32_t i=0; i<num_ara2_lane_insn; i++){
            first_entrance_bitvector.at(i) = 0;
        }

	}

    virtual void tick() {

        // 4. Officially free the lane_unit port 
        for(uint32_t port_id = 0; port_id < num_ara2_lane_insn; port_id++){

            // This will make sure if port is not empty, we are not taking the old value
            if(first_entrance_bitvector.at(port_id) == 2){
                first_entrance_bitvector.at(port_id) = 0;
            }
        }
        
        // 3. Handle response from GPR
        for(uint32_t i = 0; i < ara_gpr_rsp_ports.size(); i++){
            
            // Get port id 
            uint32_t port_id = ara_gpr_rsp_ports.front().port_id;

            // Decrement pending counter for that port_id + pop from response port 
            DT(3, "Ara-Operand_Request: Response Start : portid = " << port_id << " gpr_packet_counter = " << gpr_packet_pending_counter.at(port_id));
            gpr_packet_pending_counter.at(port_id) -= 1;
            ara_gpr_rsp_ports.pop();
            DT(3, "Ara-Operand_Request: Response End  : portid = " << port_id << " gpr_packet_counter = " << gpr_packet_pending_counter.at(port_id));
                

            // Check if all requests have been fufiled
            if(gpr_packet_pending_counter.at(port_id) == 0){

                // Return trace
                DT(3, "Ara-Operand_Request: Trace Return Start : portid = " << port_id << " gpr_packet_counter = " << gpr_packet_pending_counter.at(port_id)  );
                auto trace = this->op_req_port.at(port_id).front();
                this->op_rsp_port.at(port_id).push(trace, 1);

                // Free lane's op_req port 
                this->op_req_port.at(port_id).pop();

                // Reasoning for setting it to 2 is for the above 
                first_entrance_bitvector.at(port_id) = 2;
                DT(3, "Ara-Operand_Request: Trace Return End  : portid = " << port_id << " gpr_packet_counter = " << gpr_packet_pending_counter.at(port_id));
            }


            // TOFIX: URGENT Get the response back to the slide unit 
            
        }

        // 1. Check if request port is empty 
        for(uint32_t port_id = 0; port_id < num_ara2_lane_insn; port_id++){

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

                        // 2. Send it to the GPR 
                        DT(3, "Ara-Operand_Request: Packet Sent: portid = " << port_id << " rid = " << gpr_req.rid  );
                        ara_gpr_req_ports.push(gpr_req);
                    }
                }

                // Store the requests + initialize starting number of requests 
                ara_packet_storage.at(port_id) = Packet_List;
                gpr_packet_pending_counter.at(port_id) = ara_packet_storage.at(port_id).size();
                DT(3, "Ara-Operand_Request: Packet Overall: portid = " << port_id << " gpr_packet_counter = " << gpr_packet_pending_counter.at(port_id)  );

                // Mark finished first pass, but keep it in port to show unfinished request 
                first_entrance_bitvector.at(port_id) = 1;

                // Special case of no regfile access ==> Don't pass to RF, resolve here, otherwise insn will never commit 
                if(gpr_packet_pending_counter.at(port_id) == 0){
                    DT(3, "Ara-Operand_Request: Is special : portid = " << port_id);
                    first_entrance_bitvector.at(port_id) = 0;
                    this->op_rsp_port.at(port_id).push(trace, 1);
                    this->op_req_port.at(port_id).pop();
                }

                // Temporary Fix to bypass RF (For Debugging Purposes) 
                /*first_entrance_bitvector.at(port_id) = 0;*/
                /*this->op_rsp_port.at(port_id).push(trace, 1);*/
                /*this->op_req_port.at(port_id).pop();*/
            }
        }


        // 2. TOFIX : URGENT Slide 
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

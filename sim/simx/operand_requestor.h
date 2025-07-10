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

struct Ara_microop_Pkt {
  uint32_t port_id;
  uint32_t delay;
  uint32_t fu_type;
};



// TOFIX_ARA : Rename to ARA_Operand_Requestor 
class Operand_Requestor : public SimObject<Operand_Requestor> {

private: 
	
    // Perf counters
    uint32_t total_stalls_ = 0;
    uint32_t total_alu_queue_size = 0;
    uint32_t total_nonzero_alu_queue_time = 0;

    uint32_t num_ara2_lane_insn = NUM_ARA_LANE_INSN;
    uint32_t num_gpr_arbitration_port = NUM_ARA_GPR_PORTS;

public:

    // Take in the inputs 
    SimPort<instr_trace_t*> Input;
    SimPort<instr_trace_t*> Output;

    // Connection to lane unit 
    std::vector<SimPort<instr_trace_t*>> op_req_port;
    std::vector<SimPort<instr_trace_t*>> op_rsp_port;

    // Initial behaviour 
    std::vector<int> first_entrance_bitvector;

    // Connection to ara_gpr
    Ara_Gpr::Ptr ara_gpr_unit;
    std::vector<SimPort<AraGprPkt>> ara_gpr_req_port;
    std::vector<SimPort<AraGprPkt>> ara_gpr_rsp_port;

    // TOFIX_ARA : Don't need ara_packet_storage
    std::vector<std::vector<AraGprPkt>> ara_packet_storage; 
 
    // GPR Pending Counters
    std::vector<std::vector<int>> gpr_individual_packet_pending_counter; 
    std::vector<int> gpr_packet_pending_counter; 
    std::queue<AraGprPkt> packet_request_fifo;

    // Micro-op queue and counters
    std::vector<int> microop_packet_pending_counter; 
    std::vector<Ara_microop_Pkt> microop_alu_queue;
    std::vector<Ara_microop_Pkt> microop_mul_queue;
    std::vector<Ara_microop_Pkt> microop_other_queue;


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
        total_alu_queue_size = 0;
        total_nonzero_alu_queue_time = 0;

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

        // Create + Initialize ara_packet_storage and pending counters
        for(int i=0; i < num_ara2_lane_insn; i++){
            std::vector<AraGprPkt> temp;
            ara_packet_storage.push_back(temp);

            std::vector<int> temp2;
            gpr_individual_packet_pending_counter.push_back(temp2);
            gpr_packet_pending_counter.push_back(0);
            microop_packet_pending_counter.push_back(0);
        } 
	}

    virtual ~Operand_Requestor() {}

    virtual void reset() {
		total_stalls_ = 0;

        // Reset first entrace bitvector
        for(int i=0; i < num_ara2_lane_insn; i++){
            first_entrance_bitvector.at(i) = 0;
        }

	}

    virtual void tick() {

        /*DT(3, "----- Entered Operand_Requestor Unit -----");*/

        // 5. Officially free the port 
        for(int port_id = 0; port_id < num_ara2_lane_insn; port_id++){
            // This will make sure if port is not empty, we are not taking the old value
            if(first_entrance_bitvector.at(port_id) == 2) {
                first_entrance_bitvector.at(port_id) = 0;
            }
        }
        
        /*DT(3, "----- Entered Operand_Requestor Unit 1 -----");*/


        // 4. Once all microops finish, free that port 
        for(int port_id = 0; port_id < num_ara2_lane_insn; port_id++){
            if( (microop_packet_pending_counter.at(port_id) == 0) && (gpr_packet_pending_counter.at(port_id) == 0)){
                
                /*DT(3, "Ara-Microop-GPR FINISH for portid = " << port_id);*/

                // Check if all requests have been fufiled
                if( first_entrance_bitvector.at(port_id) == 1){

                    // Return trace
                    auto trace = this->op_req_port.at(port_id).front();
                    this->op_rsp_port.at(port_id).push(trace, 1);
                    DT(3, "Ara-Trace Return Fully : portid = " << port_id << " gpr_packet_counter = " << gpr_packet_pending_counter.at(port_id) << " TRACE : " << *trace  );

                    // Free port 
                    this->op_req_port.at(port_id).pop();

                    // This will make sure if port is not empty, we are not taking the old value
                    first_entrance_bitvector.at(port_id) = 2;
                }
            }
        }

        /*DT(3, "----- Entered Operand_Requestor Unit 2 -----");*/

        // 3. Perform microop operation 
        uint32_t ALU_lanes = NUM_ARA_ALU_LANES;
        /*uint32_t ALU_lanes = 2;*/
        uint32_t MUL_lanes = NUM_ARA_MUL_LANES;

        // Iterate through microop_queue
        for(uint32_t i = 0; i < microop_alu_queue.size(); i++){
            auto microop = microop_alu_queue.at(i);

            if(ALU_lanes != 0){
                microop_alu_queue.at(i).delay -= 1;
                ALU_lanes -= 1;

                DT(3, "Ara-Microop-ALU: Alu_counter = " << ALU_lanes << " portid = " << microop.port_id << " Index = " << i << " Leftoverdelay = " << microop_alu_queue.at(i).delay);
            } 
            DT(3, "Ara-Debug - 1: Currsize = " << microop_alu_queue.size()); 
        }


        /*DT(3, "----- Entered Operand_Requestor Unit 3 -----");*/
        
        for(uint32_t i = 0; i < microop_mul_queue.size(); i++){
            auto microop = microop_mul_queue.at(i);

            if(MUL_lanes != 0){
                microop_mul_queue.at(i).delay -= 1;
                MUL_lanes -= 1;

                DT(3, "Ara-Microop-MUL: portid = " << microop.port_id << " Leftoverdelay = " << microop_mul_queue.at(i).delay);
            }
        }

        /*DT(3, "----- Entered Operand_Requestor Unit 4 -----");*/

        for(uint32_t i = 0; i < microop_other_queue.size(); i++){
            // For the 'other' microops ==> Just clear the delay 
            auto microop = microop_other_queue.at(i);
            microop_other_queue.at(i).delay = 0;
        }

        /*DT(3, "----- Entered Operand_Requestor Unit 5 -----");*/

        // 4b. Remove microops that have finish from the queue
        // Because we are using based on functional unit, 
        // we are guranteed that the micoops that have finished are at the head of the queue
        // Hence, pop the head of the queue, and in the next interation, check the head of the queue 
        DT(3, "Ara-Debug - 2: Currsize = " << microop_alu_queue.size()); 
        
        uint32_t size = microop_alu_queue.size();
        for(uint32_t i = 0; i < size; i++){
            
            auto microop = microop_alu_queue.at(0);
            auto port_id = microop.port_id;
            if(microop.delay == 0){
                DT(3, "Ara-Microop-ALU-REACH0: Prevsize = " << microop_alu_queue.size()); 
                DT(3, "Ara-Microop-ALU-REACH0: port_id = " << port_id << " prev_pending_counter " << microop_packet_pending_counter.at(port_id)); 
                microop_alu_queue.erase(microop_alu_queue.begin());
                microop_packet_pending_counter.at(port_id) -= 1;
                DT(3, "Ara-Microop-ALU-REACH0: NEW size = " << microop_alu_queue.size()); 
                DT(3, "Ara-Microop-ALU-REACH0: port_id = " << port_id << " NEW_pending_counter " << microop_packet_pending_counter.at(port_id)); 
            } else {
                DT(3, "Ara-Microop-ALU-NOT-REACH0: Currsize = " << microop_alu_queue.size() << " Delay = " << microop.delay); 

            }
        }
        /*DT(3, "----- Entered Operand_Requestor Unit 6 -----");*/

        size = microop_mul_queue.size();
        for(uint32_t i = 0; i < size; i++){
            
            auto microop = microop_mul_queue.at(0);
            auto port_id = microop.port_id;
            if(microop.delay == 0){
                DT(3, "Ara-Microop-MUL-REACH0: Prevsize = " << microop_mul_queue.size()); 
                DT(3, "Ara-Microop-MUL-REACH0: port_id = " << port_id << " prev_pending_counter " << microop_packet_pending_counter.at(port_id)); 
                microop_mul_queue.erase(microop_mul_queue.begin());
                microop_packet_pending_counter.at(port_id) -= 1;
                DT(3, "Ara-Microop-MUL-REACH0: NEW size = " << microop_mul_queue.size()); 
                DT(3, "Ara-Microop-MUL-REACH0: port_id = " << port_id << " NEW_pending_counter " << microop_packet_pending_counter.at(port_id)); 
            }
        }


        /*DT(3, "----- Entered Operand_Requestor Unit 7 -----");*/

        size = microop_other_queue.size();
        for(uint32_t i = 0; i < size; i++){
            
            auto microop = microop_other_queue.at(0);
            auto port_id = microop.port_id;
            if(microop.delay == 0){
                DT(3, "Ara-Microop-OTHER-REACH0: Prevsize = " << microop_other_queue.size()); 
                DT(3, "Ara-Microop-OTHER-REACH0: port_id = " << port_id << " prev_pending_counter " << microop_packet_pending_counter.at(port_id)); 
                microop_other_queue.erase(microop_other_queue.begin());
                microop_packet_pending_counter.at(port_id) -= 1;
                DT(3, "Ara-Microop-OTHER-REACH0: NEW size = " << microop_other_queue.size()); 
                DT(3, "Ara-Microop-OTHER-REACH0: port_id = " << port_id << " NEW_pending_counter " << microop_packet_pending_counter.at(port_id)); 
            }
        }

        /*DT(3, "----- Entered Operand_Requestor Unit 8 -----");*/

        // 2. Handle response from GPR
        for(int i = 0; i < num_gpr_arbitration_port; i++){
            
            // Check if response exists
            if(!ara_gpr_rsp_port.at(i).empty()){
                
                // Get port id 
                uint32_t port_id = ara_gpr_rsp_port.at(i).front().port_id;
                uint32_t vr_id   = ara_gpr_rsp_port.at(i).front().vr_id; 

                // Decrement pending counter for that port_id + pop from response port 
                /*DT(3, "Ara-Operand_Request: Response Start : portid = " << port_id << " gpr_packet_counter = " << gpr_packet_pending_counter.at(port_id)  );*/
                gpr_packet_pending_counter.at(port_id) -= 1;
                gpr_individual_packet_pending_counter.at(port_id).at(vr_id) -= 1;

                ara_gpr_rsp_port.at(i).pop();
                DT(3, "Ara-GPR-Operand_Response: portid = " << port_id << " gpr_packet_counter = " << gpr_packet_pending_counter.at(port_id) << " Leftover at vrid = "<< gpr_individual_packet_pending_counter.at(port_id).at(vr_id));
                
                // Check if requests for 1 microop has been fufiled ==> Add to microop queue 
                if(gpr_individual_packet_pending_counter.at(port_id).at(vr_id) == 0){

                    // Construct packet 
                    Ara_microop_Pkt packet;
                    packet.port_id = port_id;
                    
                    // Get appropriate delay 
                    auto trace = this->op_req_port.at(port_id).front();
                    DT(3, "Ara-Microop Construction: portid = " << port_id << " Trace Type " << trace->vpu_type);
                    
                    uint32_t delay = 0;
                    uint32_t fu_type = 0;

                    switch (trace->vpu_type) {
                        case VpuType::VSET:

                            // Push to microop queue 
                            microop_other_queue.push_back(packet);
                            DT(3, "Ara-Microop Construction: Push to OTHER : portid = " << port_id << " Indiv Packet index :" << vr_id);

                            break;
                        case VpuType::ARITH:
                        case VpuType::ARITH_R:
                            packet.delay = 1;
                            packet.fu_type = 1;

                            // Push to microop queue 
                            microop_alu_queue.push_back(packet);
                            DT(3, "Ara-Microop Construction: Push to ALU : portid = " << port_id << " Indiv Packet index :" << vr_id);

                            break;
                        case VpuType::IMUL:
                            packet.delay = LATENCY_IMUL;
                            packet.fu_type = 2;

                            // Push to microop queue 
                            microop_mul_queue.push_back(packet);
                            DT(3, "Ara-Microop Construction: Push to MUL : portid = " << port_id << " Indiv Packet index :" << vr_id);

                            break;
                        case VpuType::IDIV:
                            packet.delay = XLEN;
                            packet.fu_type = 3;

                            // Push to microop queue 
                            microop_other_queue.push_back(packet);
                            DT(3, "Ara-Microop Construction: Push to OTHER : portid = " << port_id << " Indiv Packet index :" << vr_id);

                            break;
                        case VpuType::FNCP:
                        case VpuType::FNCP_R:
                            packet.delay = 2;
                            packet.fu_type = 4;

                            // Push to microop queue 
                            microop_other_queue.push_back(packet);
                            DT(3, "Ara-Microop Construction: Push to OTHER : portid = " << port_id << " Indiv Packet index :" << vr_id);

                            break;
                        case VpuType::FMA:
                        case VpuType::FMA_R:
                            packet.delay = LATENCY_FMA;
                            packet.fu_type = 5;

                            // Push to microop queue 
                            microop_other_queue.push_back(packet);
                            DT(3, "Ara-Microop Construction: Push to OTHER : portid = " << port_id << " Indiv Packet index :" << vr_id);

                            break;
                        case VpuType::FDIV:
                            packet.delay = LATENCY_FDIV;
                            packet.fu_type = 6;

                            // Push to microop queue 
                            microop_other_queue.push_back(packet);
                            DT(3, "Ara-Microop Construction: Push to OTHER : portid = " << port_id << " Indiv Packet index :" << vr_id);

                            break;
                        case VpuType::FSQRT:
                            packet.delay = LATENCY_FSQRT;
                            packet.fu_type = 7;

                            // Push to microop queue 
                            microop_other_queue.push_back(packet);
                            DT(3, "Ara-Microop Construction: Push to OTHER : portid = " << port_id << " Indiv Packet index :" << vr_id);

                            break;
                        case VpuType::FCVT:
                            packet.delay = LATENCY_FCVT;
                            packet.fu_type = 8;

                            // Push to microop queue 
                            microop_other_queue.push_back(packet);
                            DT(3, "Ara-Microop Construction: Push to OTHER : portid = " << port_id << " Indiv Packet index :" << vr_id);

                            break;
                        default:
                            std::abort();
                    }
                }
            }
        }


        /*DT(3, "----- Entered Operand_Requestor Unit 9 -----");*/

        // 1a. Check if request port is empty 
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
                // TOFIX_ARA : The concept of EW and VLmul 
                uint32_t gpr_pending_requests = 0;
                uint32_t VL_count = VLEN/XLEN;
                VL_count = VL_count / NUM_ARA_LANES; 
                // TORETURN : For stress testing
                VL_count = NUM_ARA_VL_COUNT;

                // Other stuff to reset  
                int microop_counter = 0;
                gpr_individual_packet_pending_counter.at(port_id).clear();

                // NOTE : Don't change the loop arrangement (Do iteration for outer, then NUM_SRC_REGS for inner) 
                for(uint32_t iteration = 0; iteration < VL_count; iteration++){

                    int gpr_indv_counter = 0;

                    for (uint32_t i = 0; i < NUM_SRC_REGS; i++) {
                        if (opd_to_fetch.test(i)) {
                            AraGprPkt gpr_req;
                            gpr_req.rid = trace->src_regs[i].id();
                            gpr_req.vr_id = iteration;
                            gpr_req.port_id = port_id;
                            
                            // TORETURN : For stress testing
                            /*gpr_req.vr_id = 0;*/
    
                            // Put it into request fifo 
                            packet_request_fifo.push(gpr_req);

                            // Add to gpr counter
                            gpr_pending_requests += 1;
                            gpr_indv_counter += 1;
                            
                            DT(3, "Ara-Operand_Request: Packet Construct: portid = " << port_id << " gpr_rid = " << gpr_req.rid << " gpr_vrid = " << gpr_req.vr_id  );
                        }
                    }
                    
                    // TOFIX_ARA : Check whether masking affects the microop 
                    microop_counter += 1;

                    // Add gpr_indv_counter to overall block
                    gpr_individual_packet_pending_counter.at(port_id).push_back(gpr_indv_counter);
                    DT(3, "Ara-Operand_Request: Packet Construct: portid = " << port_id << " Indiv packet_counter = " << gpr_indv_counter << " Indiv queue size = " << gpr_individual_packet_pending_counter.at(port_id).size()); 
                }
    
                // Store the requests + initialize starting number of requests 
                gpr_packet_pending_counter.at(port_id) = gpr_pending_requests; 
                microop_packet_pending_counter.at(port_id) = microop_counter; 
                DT(3, "Ara-Operand_Request: GPR     Packet Construct: portid = " << port_id << " Overall packet_counter = " << gpr_packet_pending_counter.at(port_id));
                DT(3, "Ara-Operand_Request: Microop Packet Construct: portid = " << port_id << " Overall packet_counter = " << microop_packet_pending_counter.at(port_id));

                // Mark finished first pass, but keep it in port to show unfinished request 
                first_entrance_bitvector.at(port_id) = 1;

                // Special case of no regfile access ==> Don't pass to RF, resolve here, otherwise insn will never commit 
                if(gpr_packet_pending_counter.at(port_id) == 0){
                    first_entrance_bitvector.at(port_id) = 0;
                    this->op_rsp_port.at(port_id).push(trace, 1);
                    this->op_req_port.at(port_id).pop();
                }

                // Temporary Fix 
                /*first_entrance_bitvector.at(port_id) = 0;*/
                /*this->op_rsp_port.at(port_id).push(trace, 1);*/
                /*this->op_req_port.at(port_id).pop();*/
            }
        }


        /*DT(3, "----- Entered Operand_Requestor Unit 10 -----");*/

        // 1b. Move requests from fifo to gpr ports  
        for(int i=0; i < num_gpr_arbitration_port; i++){
            // 2a. Check if port is free and Check if there are requests 
            if(ara_gpr_req_port.at(i).empty() && !packet_request_fifo.empty()){
                AraGprPkt gpr_req = packet_request_fifo.front();
                DT(3, "Ara-Operand_Request: GPR Req: portid = " << gpr_req.port_id << " gpr_packet_counter = " << gpr_packet_pending_counter.at(gpr_req.port_id)  );
                ara_gpr_req_port.at(i).push(gpr_req);
                packet_request_fifo.pop();
            }
        }


        /*DT(3, "----- Finished Operand_Requestor Unit -----");*/
    
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

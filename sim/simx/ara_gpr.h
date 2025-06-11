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
  uint32_t rid;         // The vector id  (E.g: v0, v1 v2)
  uint32_t vr_id;       // The subvector register id (E.g: v00 --> 0, v01 --> 1, v02 --> 2 )

  friend std::ostream& operator<<(std::ostream& os, const AraGprPkt& req) {
    os << "123123";
    return os;
  }

};



class Ara_Gpr : public SimObject<Ara_Gpr> {

private: 
	uint32_t total_stalls_ = 0;

    // TOFIX : Get this from operand requestor 
    // Also same as the number of ara gpr banks
    uint32_t num_gpr_arbitration_port = NUM_ARA_GPR_PORTS;

public:

    // Take in the inputs 
    SimPort<instr_trace_t*> Input;
    SimPort<instr_trace_t*> Output;

    std::vector<SimPort<AraGprPkt>> ara_gpr_req_port;
    std::vector<SimPort<AraGprPkt>> ara_gpr_rsp_port;

    std::vector<AraGprPkt> packet_collector;


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
    
        /*DT(3, "----- Entered Ara-Gpr Unit -----");*/

        // 2. Simulate bank conflicts and response
        for(uint32_t bank = 0; bank < num_gpr_arbitration_port; bank++){

            if(packet_collector.size() == 0){
                break;
            }

            uint32_t i = 0;
            while (i < packet_collector.size()) {
                
                // Calculate bank following barber shift formula 
                AraGprPkt gpr_rsp = packet_collector.at(i);
                uint32_t bank_x = (gpr_rsp.rid + gpr_rsp.vr_id) % num_gpr_arbitration_port;

                if (bank == bank_x) {
                    ara_gpr_rsp_port.at(bank).push(gpr_rsp, 2);
                    DT(3, "Ara-Reg-File : Bank Match: bank=" << bank << ", gpr_rsp.rid=" << gpr_rsp.rid << ", gpr_Rsp.vr_id=" << gpr_rsp.vr_id << ", port_id=" << gpr_rsp.port_id );
                    packet_collector.erase(packet_collector.begin() + i);
                    break;  // break to next bank
                } else {
                    ++i;
                }
            }
        }

        // 1. Put all packets into packet_collector
        for(int i = 0; i < num_gpr_arbitration_port; i++){ 

            // 1a. Check if request port is empty 
            if(!ara_gpr_req_port.at(i).empty()) {

                // Put requests into collector 
                AraGprPkt gpr_req = ara_gpr_req_port.at(i).front();
                packet_collector.push_back(gpr_req);
            
                // Pop from request port
                DT(3, "Ara-Reg-File : gpr port num = " << i << " port_id " << gpr_req.port_id );
                ara_gpr_req_port.at(i).pop();
            }
        }

        // Temporary Fix (If needed, the following code can be used as replacement for debug) 
        /*
        for(int i = 0; i < num_gpr_arbitration_port; i++){ 

            // 1a. Check if request port is empty 
            if(!ara_gpr_req_port.at(i).empty()) {

                // Send Response back 
                AraGprPkt gpr_rsp = ara_gpr_req_port.at(i).front();
                ara_gpr_rsp_port.at(i).push(gpr_rsp, 1);
            
                // Pop from request port
                DT(3, "Ara-Reg-File : gpr port num = " << i << " port_id " << gpr_rsp.port_id );
                ara_gpr_req_port.at(i).pop();
            }
        }
        */


        /*DT(3, "----- Finished Ara-Gpr Unit -----");*/
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

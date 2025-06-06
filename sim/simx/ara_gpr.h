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
    os << "AraGprPkt ......... ";
    return os;
  }

};


class Ara_Gpr : public SimObject<Ara_Gpr> {

private: 
	uint32_t total_stalls_ = 0;
    uint32_t NUM_ARA_GPR_BANKS = 8;

public:

    // Take in the inputs 
    SimPort<instr_trace_t*> Input;
    SimPort<instr_trace_t*> Output;

    SimPort<AraGprPkt> ara_gpr_req_ports;
    SimPort<AraGprPkt> ara_gpr_rsp_ports;

    std::vector<AraGprPkt> packet_collector;


    Ara_Gpr(const SimContext& ctx)
			: SimObject<Ara_Gpr>(ctx, "unit")
			, Input(this)
			, Output(this)
            , ara_gpr_req_ports(this)
            , ara_gpr_rsp_ports(this)

    {
		total_stalls_ = 0;
	}

    virtual ~Ara_Gpr() {}

    virtual void reset() {
		total_stalls_ = 0;
	}

    virtual void tick() {
    

        // 1. Copy all requests into packet collector
        for(uint32_t i = 0; i < ara_gpr_req_ports.size(); i++){

            AraGprPkt gpr_req = ara_gpr_req_ports.front();
            packet_collector.push_back(gpr_req);
            ara_gpr_req_ports.pop();

        }

        // 2. Perform bank conflict algo
        for(uint32_t bank = 0; bank < NUM_ARA_GPR_BANKS; bank++){

            for(uint32_t i = 0; i < packet_collector.size(); i++){

                // 2a. Calculate bank
                AraGprPkt gpr_rsp = packet_collector.at(i);
                uint32_t bank_x = gpr_rsp.rid % NUM_ARA_GPR_BANKS;

                // 2b. Check if banks match
                if(bank == bank_x){

                    // Send to output and remove from packet_collector
                    ara_gpr_rsp_ports.push(gpr_rsp, 1);
                    DT(3, "Ara-Reg-File : gpr req num = " << i << " port_id " << gpr_rsp.port_id );
                    packet_collector.erase(packet_collector.begin() + i);

                    // Skip to calculate next bank
                    break;
                }
            }
        }

        // Temporary Fix for gprf (Debugging purposes)
        /*
        for(uint32_t i = 0; i < ara_gpr_req_ports.size(); i++){ 
            // Send Response back 
            AraGprPkt gpr_rsp = ara_gpr_req_ports.front();
            ara_gpr_rsp_ports.push(gpr_rsp, 1);
            
            // Pop from request port
            DT(3, "Ara-Reg-File : gpr req num = " << i << " port_id " << gpr_rsp.port_id );
            ara_gpr_req_ports.pop();
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

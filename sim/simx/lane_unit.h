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

namespace vortex {

class Lane_Unit : public SimObject<Lane_Unit> {

private: 
		uint32_t total_stalls_ = 0;

public:

    // Take in the inputs 
    SimPort<instr_trace_t*> Input;
    SimPort<instr_trace_t*> Output;


    SimPort<instr_trace_t*> lane_opreq_req_port;
    SimPort<instr_trace_t*> lane_opreq_rsp_port;

    Lane_Unit(const SimContext& ctx)
			: SimObject<Lane_Unit>(ctx, "lane_unit")
			, Input(this)
			, Output(this)
            , lane_opreq_rsp_port(this)
            , lane_opreq_req_port(this)
    {
			total_stalls_ = 0;
	}

    virtual ~Lane_Unit() {}

    virtual void reset() {
			total_stalls_ = 0;
		}

    virtual void tick() {


        if (lane_opreq_req_port.empty())
			return;
		auto trace = lane_opreq_req_port.front();
		lane_opreq_rsp_port.push(trace, 1);
		lane_opreq_req_port.pop();

        // Simulate Bank conflicts in lane unit 
        /*
		for (int i = 0; i < NUM_SRC_REGS; ++i) {
			uint32_t x_rid = trace->src_regs[i].id();
			if (x_rid == 0)
				continue; // skip x0 or empty
			for (int j = i + 1; j < NUM_SRC_REGS; ++j) {
				uint32_t y_rid = trace->src_regs[j].id();
				if (y_rid == 0)
					continue; // skip x0 or empty
				int bank_x = x_rid % NUM_BANKS;
				int bank_y = y_rid % NUM_BANKS;
				if (bank_x == bank_y) {
					++stalls;
				}
			}
		}
        */

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

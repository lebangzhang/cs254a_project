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
#include <unordered_map>
#include <vector>

namespace vortex {

class Scoreboard {
public:

	struct reg_use_t {
		RegType  reg_type;
		uint32_t reg_id;
		FUType   fu_type;
		OpType   op_type;
		uint64_t uuid;
	};

	Scoreboard(const Arch &arch)
	: in_use_regs_(arch.num_warps()) {
		for (auto& in_use_reg : in_use_regs_) {
			in_use_reg.resize((int)RegType::Count);
		}
		this->reset();
	}

	void reset() {
		for (auto& in_use_reg : in_use_regs_) {
			for (auto& mask : in_use_reg) {
				mask.reset();
			}
		}
		owners_.clear();
	}

	bool in_use(instr_trace_t* trace, std::vector<reg_use_t>* out = nullptr) const {
		bool found = false;
		if (trace->wb) {
			assert(trace->dst_reg.type != RegType::None);
			if (in_use_regs_.at(trace->wid).at((int)trace->dst_reg.type).test(trace->dst_reg.idx)) {
				if (out) {
					out->push_back(make_reg_use(trace->dst_reg, trace));
				}
				found = true;
			}
		}
		for (uint32_t i = 0; i < trace->src_regs.size(); ++i) {
			if (trace->src_regs[i].type != RegType::None) {
				if (in_use_regs_.at(trace->wid).at((int)trace->src_regs[i].type).test(trace->src_regs[i].idx)) {
					if (out) {
						out->push_back(make_reg_use(trace->src_regs[i], trace));
					}
					found = true;
				}
			}
		}
	#ifdef EXT_V_ENABLE
		// explicit handling of vmask dependencies
		bool use_vmask = false;
		if (std::get_if<VlsType>(&trace->op_type)) {
			auto trace_data = std::dynamic_pointer_cast<VecUnit::MemTraceData>(trace->data);
			use_vmask = trace_data->is_masked;
		} else
		if (std::get_if<VopType>(&trace->op_type)) {
			auto trace_data = std::dynamic_pointer_cast<VecUnit::ExeTraceData>(trace->data);
			use_vmask = trace_data->is_masked;
		}
		if (use_vmask) {
			RegOpd v0{RegType::Vector, 0};
			if (in_use_regs_.at(trace->wid).at((int)v0.type).test(v0.idx)) {
				if (out) {
					out->push_back(make_reg_use(v0, trace));
				}
				found = true;
			}
		}
	#endif // EXT_V_ENABLE
		return found;
	}

	void reserve(instr_trace_t* trace) {
		uint32_t reg_id = get_reg_id(trace->dst_reg, trace->wid);
		assert(trace->wb);
		in_use_regs_.at(trace->wid).at((int)trace->dst_reg.type).set(trace->dst_reg.idx);
		assert(owners_.count(reg_id) == 0);
		owners_[reg_id] = trace;
	}

	void release(instr_trace_t* trace) {
		uint32_t reg_id = get_reg_id(trace->dst_reg, trace->wid);
		assert(trace->wb);
		assert(in_use_regs_.at(trace->wid).at((int)trace->dst_reg.type).test(trace->dst_reg.idx));
		in_use_regs_.at(trace->wid).at((int)trace->dst_reg.type).reset(trace->dst_reg.idx);
		assert(owners_.count(reg_id) != 0);
		owners_.erase(reg_id);
	}

private:

  static uint32_t get_reg_id(const RegOpd& reg, uint32_t wid) {
    return (wid << RegOpd::ID_BITS) | reg.id();
  }

	reg_use_t make_reg_use(const RegOpd& reg, instr_trace_t* trace) const {
		uint32_t reg_id = get_reg_id(reg, trace->wid);
		auto owner = owners_.at(reg_id);
		return {trace->dst_reg.type, trace->dst_reg.idx, owner->fu_type, owner->op_type, owner->uuid};
	}

	std::vector<std::vector<RegMask>> in_use_regs_;
	std::unordered_map<uint32_t, instr_trace_t*> owners_;
};

}

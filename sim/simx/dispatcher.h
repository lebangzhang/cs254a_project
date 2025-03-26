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
#include <queue>
#include <vector>

namespace vortex {

class Dispatcher : public SimObject<Dispatcher> {
public:
	std::vector<SimPort<instr_trace_t*>> Outputs;

	Dispatcher(const SimContext& ctx, const Arch& arch, uint32_t buf_size, uint32_t block_size, uint32_t num_lanes)
		: SimObject<Dispatcher>(ctx, "Dispatcher")
		, Outputs(ISSUE_WIDTH, this)
		, Inputs_(ISSUE_WIDTH, this)
		, arch_(arch)
		, queues_(ISSUE_WIDTH, std::queue<instr_trace_t*>())
		, buf_size_(buf_size)
		, block_size_(block_size)
		, num_lanes_(num_lanes)
		, num_blocks_(ISSUE_WIDTH / block_size)
		, num_packets_(arch.num_threads() / num_lanes)
		, batch_idx_(0)
		, block_states_(block_size)
	{}

	virtual ~Dispatcher() {}

	virtual void reset() {
		batch_idx_ = 0;
		for (auto& bs : block_states_) {
			bs.clear();
		}
	}

	virtual void tick() {
		// process input queues
		for (uint32_t i = 0; i < ISSUE_WIDTH; ++i) {
			auto& queue = queues_.at(i);
			if (queue.empty())
				continue;
			auto trace = queue.front();
			Inputs_.at(i).push(trace, 1);
			queue.pop();
		}

		// round-robin select (block_size) traces form input queues
		// and issue them in parallel to output queues
		uint32_t block_sent = 0;
		for (uint32_t b = 0; b < block_size_; ++b) {
			uint32_t i = batch_idx_ * block_size_ + b;
			auto& input = Inputs_.at(i);
			if (input.empty()) {
				++block_sent;
				continue;
			}
			auto& output = Outputs.at(i);
			auto trace = input.front();

			// check if trace should be split
			if (num_packets_ != 1) {
				auto& state = block_states_.at(b);
				if (state.pid == -1) {
					++block_sent;
					continue;
				}
				if (state.pid == 0) {
					// backup trace tmask
					state.tmask = trace->tmask;
				}

				// calculate current packet start and end
				int start(-1), end(-1);
				for (uint32_t j = state.pid * num_lanes_, n = arch_.num_threads(); j < n; ++j) {
					if (!state.tmask.test(j))
						continue;
					if (start == -1)
						start = j;
					end = j;
				}
				start /= num_lanes_;
				end /= num_lanes_;

				// issue new packet
				ThreadMask tmask;
				for (int j = start * num_lanes_, n = j + num_lanes_; j < n; ++j) {
					tmask[j] = state.tmask[j];
				}
				trace->tmask = tmask;
				trace->pid = start;
				trace->sop = (0 == state.pid);
				trace->eop = (start == end);

				// advance packet index
				state.pid = start + 1;
				if (start == end) {
					state.pid = -1; // mark this block as done
					input.pop();
					++block_sent;
				}
			} else {
				// issue the trace
				input.pop();
				++block_sent;
			}
			DT(3, "pipeline-dispatch: " << *trace);
			output.push(trace, 1);
		}

		// we move to the next batch only when the current batch has fully dispatched
		if (block_sent == block_size_) {
			// round-robin batch selection
			batch_idx_ = (batch_idx_ + 1) % num_blocks_;
			for (auto& bs : block_states_) {
				bs.clear();
			}
		}
	};

	bool push(uint32_t issue_index, instr_trace_t* trace) {
		auto& queue = queues_.at(issue_index);
		if (queue.size() >= buf_size_)
			return false;
		queue.push(trace);
		return true;
	}

private:
	struct block_state_t {
		ThreadMask tmask;
		int pid;

		block_state_t() { this->clear(); }

		void clear() {
			tmask.reset();
			pid = 0;
		}
	};

	std::vector<SimPort<instr_trace_t*>> Inputs_;
	const Arch& arch_;
	std::vector<std::queue<instr_trace_t*>> queues_;
	uint32_t buf_size_;
	uint32_t block_size_;
	uint32_t num_lanes_;
	uint32_t num_blocks_;
	uint32_t num_packets_;
	uint32_t batch_idx_;
	std::vector<block_state_t> block_states_;
};

}

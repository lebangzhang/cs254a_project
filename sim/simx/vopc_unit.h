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

class Core;

class VOpcUnit : public SimObject<VOpcUnit> {
public:
  SimChannel<instr_trace_t*> Input;
  SimChannel<instr_trace_t*> Output;

  VOpcUnit(const SimContext &ctx, const char* name, Core* core);

  virtual ~VOpcUnit();

  virtual void reset();

  virtual void tick();

  void writeback(instr_trace_t* trace);

  uint32_t total_stalls() const {
    return total_stalls_;
  }

private:

  uint32_t tree_height(uint32_t n);

  void compute_red_vector_timing_counter(uint32_t red_tree_h);
  void send_last_trace(instr_trace_t* trace);
  void send_uop_trace(instr_trace_t* trace);
  uint32_t compute_vector_stalls(instr_trace_t* trace);
  uint32_t compute_scalar_stalls(instr_trace_t* trace);
  uint32_t compute_total_gpr_requests(instr_trace_t* trace);
  uint32_t compute_total_vgpr_requests(instr_trace_t* trace);
  void translate(instr_trace_t* trace);
  void lsu_flush(instr_trace_t* trace);

  Core*    core_; 
  uint32_t total_stalls_ = 0;
  uint32_t total_gpr_requests = 1;
  uint32_t curr_vgpr_requests = 0;
  uint32_t scalar_stalls = 0;
  uint32_t vector_stalls = 0; 

  uint32_t curr_vector_timing_counter = 0;
  uint32_t uops_vector_timing_counter = 0;

  uint32_t red_tree_height = 0;
  uint32_t curr_red_tree_h = 0;

  Word     active_PC_;

  bool     instr_pending_ = false;
  bool     wb_rsp_received = false;
  bool     lsu_flush_ = false;
  bool     is_reduction_ = false;
  bool     is_vset = false;
  bool     done = false;
};

} // namespace vortex

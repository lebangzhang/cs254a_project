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

#include "vopc_unit.h"
#include "core.h"

using namespace vortex;

VOpcUnit::VOpcUnit(const SimContext &ctx, Core* core)
  : SimObject<VOpcUnit>(ctx, "vopc-unit")
  , Input(this)
  , Output(this)
  , core_(core) {
  this->reset();
}

VOpcUnit::~VOpcUnit() {}

void VOpcUnit::reset() {
  // Stall variables
  total_stalls_ = 0;
  scalar_stalls = 0;
  vector_stalls = 0; 

  // VRF variables
  total_iterations = 3;
  curr_iterations = 0;

  // Vlmul and Vl
  vl_counter_ = 0; 
  vlmul_counter_ = 0; 
  curr_vlmul_counter = 0;

  // True/False variables
  instr_pending_ = false; 
  lsu_flush_ = false; 
  is_reduction_ = false;
}

void VOpcUnit::tick() {

  // process incoming instructions
  if (Input.empty())
    return;
  auto trace = Input.front();

  // First Entry initialization
  if(!instr_pending_) {

    // Calculate total iterations to RF needed
    curr_iterations = 0;
    total_iterations = 3;

    // Calculate Expected RF stalls 
    scalar_stalls = this->compute_scalar_stalls(trace);
    vector_stalls = this->compute_vector_stalls(trace);
    
    // Reduction and Vlmul  
    if (trace->fu_type == FUType::VPU) {
      auto trace_data = std::dynamic_pointer_cast<VecUnit::ExeTraceData>(trace->data);
      auto vpu_op = trace_data->vpu_op;
      is_reduction_ = (vpu_op >= VpuOpType::ARITH_R);

      vl_counter_ = trace_data->vl;
      vlmul_counter_ = trace_data->vlmul;
    }

    // Remove first entry in next CC
    instr_pending_ = true;
  }

  // Scalar instruction
  if(trace->fu_type != FUType::VPU) {

    // Issue last trace to Execution
    this->Output.push(trace, 2 + scalar_stalls);
    DT(3, "*** VOPC_scalar_trace: " << *trace);
    Input.pop();

  // Vector instruction
  } else {

    // Standard Arithmetic operation 
    if(!is_reduction_){

      vector_stalls = this->compute_vector_stalls(trace);
      
      // Perform standard Arithmetic operation 
      if (curr_iterations < total_iterations) {
        this->vector_std_arith_insn(trace);
        curr_iterations += 1;
      } else {
        // Final Vector Trace 
        this->vector_final_insn(trace);
      }

    // Reduction and Slide instruction
    } else {
       this->vector_final_insn(trace);
    }
  }
}

void VOpcUnit::vector_std_arith_insn(instr_trace_t* trace) {

  // Only account for scalar_stalls in first iteration, otherwise stalls is due to VRF 
  uint32_t stalls  = 0; 
  if(curr_iterations == 0){
    stalls = std::max(scalar_stalls, vector_stalls);
  } else {
    stalls = vector_stalls;
  }

  // Not last trace --> Allocate insn trace 
  auto trace_alloc = core_->trace_pool().allocate(1);
  auto new_trace = new (trace_alloc) instr_trace_t(*trace);

  if (trace->fu_type == FUType::VPU) {
    // translate VPU instructions
    this->translate(new_trace);
  }
  new_trace->wb = false; // disable scoreboard release

  // Check if is LSU instruction
  this->lsu_flush(new_trace);

  // Send trace to Execution
  this->Output.push(new_trace, 2 + stalls); 
  DT(3, "*** VOPC_issued_trace: curr_iter=" << curr_iterations << ", total_iter=" << total_iterations << ", " << *new_trace);
}


void VOpcUnit::vector_final_insn(instr_trace_t* trace) {

  uint32_t stalls = vector_stalls;

  if (trace->fu_type == FUType::VPU) {
    // translate VPU instructions
    this->translate(trace);
  }

  // Check if is LSU instruction 
  this->lsu_flush(trace);
      
  // Issue last trace to Execution
  this->Output.push(trace, 2 + stalls);
  DT(3, "*** VOPC_final_trace: " << *trace);
  Input.pop();

  // Reset Parameters for next insn
  total_stalls_ = 0;
  total_iterations = 3;
  curr_iterations = 0;
  scalar_stalls = 0;
  vector_stalls = 0; 

  vl_counter_ = 0;
  vlmul_counter_ = 0; 
  curr_vlmul_counter = 0;

  instr_pending_ = false;
  lsu_flush_ = false;
  is_reduction_ = false;
}



uint32_t VOpcUnit::compute_vector_stalls(instr_trace_t* trace) {

  uint32_t vector_stalls = 0;

  // calculate bank conflict stalls
  for (uint32_t i = 0; i < NUM_SRC_REGS; ++i) {
    for (uint32_t j = i + 1; j < NUM_SRC_REGS; ++j) {
      if ((trace->src_regs[i].type == RegType::None) || (trace->src_regs[j].type == RegType::None))
          continue;
      if ((trace->src_regs[i].type == RegType::Integer && trace->src_regs[i].id() == 0)
        || (trace->src_regs[j].type == RegType::Integer && trace->src_regs[j].id() == 0))
          continue; // skip x0
        
      // bank conflict
      uint32_t bank_i = trace->src_regs[i].idx % NUM_GPR_BANKS;
      uint32_t bank_j = trace->src_regs[j].idx % NUM_GPR_BANKS;
      if (bank_i == bank_j) {
        if (trace->src_regs[i].type == RegType::Vector
          && trace->src_regs[j].type == RegType::Vector) {
          ++vector_stalls;
        }
      }
    }
  }
  
  /*assert(vector_stalls < 2);*/
  DT(3, "vector_stalls: " << vector_stalls << " " << *trace);
  return vector_stalls;
}



uint32_t VOpcUnit::compute_scalar_stalls(instr_trace_t* trace) {

  uint32_t scalar_stalls = 0;

  // calculate bank conflict stalls
  for (uint32_t i = 0; i < NUM_SRC_REGS; ++i) {
    for (uint32_t j = i + 1; j < NUM_SRC_REGS; ++j) {
      if ((trace->src_regs[i].type == RegType::None) || (trace->src_regs[j].type == RegType::None))
          continue;
      if ((trace->src_regs[i].type == RegType::Integer && trace->src_regs[i].id() == 0)
        || (trace->src_regs[j].type == RegType::Integer && trace->src_regs[j].id() == 0))
          continue; // skip x0
            
      // bank conflict
      uint32_t bank_i = trace->src_regs[i].idx % NUM_GPR_BANKS;
      uint32_t bank_j = trace->src_regs[j].idx % NUM_GPR_BANKS;
      if (bank_i == bank_j) {
        if (trace->src_regs[i].type != RegType::Vector
          && trace->src_regs[j].type != RegType::Vector) {
          ++scalar_stalls;
        }
      }
    }
  }
  
  /*assert(scalar_stalls < 1);*/
  DT(3, "scalar_stalls: " << scalar_stalls << " "<< *trace);
  return scalar_stalls;
}




void VOpcUnit::translate(instr_trace_t* trace) {
  auto trace_data = std::dynamic_pointer_cast<VecUnit::ExeTraceData>(trace->data);
  auto vpu_op = trace_data->vpu_op;
  switch (vpu_op) {
  case VpuOpType::VSET:
    // no convertion
    break;
  case VpuOpType::ARITH:
  case VpuOpType::ARITH_R:
    trace->fu_type = FUType::ALU;
    trace->op_type = AluType::ADD;
    break;
  case VpuOpType::IMUL:
    trace->fu_type = FUType::ALU;
    trace->op_type = MdvType::MUL;
    break;
  case VpuOpType::IDIV:
    trace->fu_type = FUType::ALU;
    trace->op_type = MdvType::DIV;
    break;
  case VpuOpType::FMA:
  case VpuOpType::FMA_R:
    trace->fu_type = FUType::FPU;
    trace->op_type = FpuType::FMADD;
    break;
  case VpuOpType::FDIV:
    trace->fu_type = FUType::FPU;
    trace->op_type = FpuType::FDIV;
    break;
  case VpuOpType::FSQRT:
    trace->fu_type = FUType::FPU;
    trace->op_type = FpuType::FSQRT;
    break;
  case VpuOpType::FCVT:
    trace->fu_type = FUType::FPU;
    trace->op_type = FpuType::F2I;
    break;
  case VpuOpType::FNCP:
  case VpuOpType::FNCP_R:
    trace->fu_type = FUType::FPU;
    trace->op_type = FpuType::FCMP;
    break;
  default:
    assert(false);
  }
}


void VOpcUnit::lsu_flush(instr_trace_t* trace) {
  if (trace->fu_type != FUType::LSU)
    return;
  if (lsu_flush_) {
    trace->data = nullptr;
    return;
  }
  lsu_flush_ = true;
}



void VOpcUnit::writeback(instr_trace_t* /*trace*/) {
  //--
}

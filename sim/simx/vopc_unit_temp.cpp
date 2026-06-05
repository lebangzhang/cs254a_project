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
  total_vgpr_requests = 3;
  curr_vgpr_requests = 0;

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

    // Calculate total vgpr_requests to RF needed
    curr_vgpr_requests = 0;
    total_vgpr_requests = compute_total_vgpr_requests(trace);

    // Calculate Expected RF stalls 
    scalar_stalls = this->compute_scalar_stalls(trace);
    vector_stalls = this->compute_vector_stalls(trace, 0);
    
    // Check if vector insn -> Then compute Reduction and Vlmul parameters
    if (trace->fu_type == FUType::VPU) {
      auto trace_data = std::dynamic_pointer_cast<VecUnit::ExeTraceData>(trace->data);
      auto vpu_op = trace_data->vpu_op;
      is_reduction_ = (vpu_op >= VpuOpType::ARITH_R);
      
      active_PC_ = trace->PC;

      vl_counter_ = trace_data->vl;
      vlmul_counter_ = trace_data->vlmul;
      curr_vlmul_counter = 0;
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

    // Standard Vector Arithmetic operation 
    if(!is_reduction_){

      // If not last vlmul iteration ==> Do regular arith instruction
      if (curr_vlmul_counter != vlmul_counter_){
        this->vector_std_trace(trace);
        curr_vgpr_requests += 1;

        // VRF iteration has finished ==> next vlmul
        if(curr_vgpr_requests == total_vgpr_requests){
          // Reset VRF iteration for next vlmul 
          curr_vgpr_requests = 0;
          curr_vlmul_counter += 1; 

          // Recompute stalls for each vlmul
          vector_stalls = this->compute_vector_stalls(trace, curr_vlmul_counter);
        }
      
      // last vlmul iteration
      } else {
         
        // Not last VRF iteration ==> Perform regular arith insn
        if(curr_vgpr_requests != total_vgpr_requests){
          this->vector_std_trace(trace);
          curr_vgpr_requests += 1;
        
        // Last VRF iteration and last vlmul iteration ==> Last trace
        } else {
          this->vector_final_trace(trace);
        }
      }
    // Reduction and Slide instruction
    } else {
       this->vector_final_trace(trace);
    }
  }
}



/*void VOpcUnit::reduction_level_0(instr_trace_t* trace) {*/
/*}*/



void VOpcUnit::vector_std_trace(instr_trace_t* trace) {

  // Only account for scalar_stalls in first iteration, otherwise stalls is due to VRF 
  uint32_t stalls  = 0; 
  if(curr_vgpr_requests == 0){
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
  new_trace->wb = false;  // internal uop, not an architectural retirement point
  new_trace->instr_eop = false;

  // Check if is LSU instruction
  this->lsu_flush(new_trace);

  // Send trace to Execution
  this->Output.push(new_trace, 2 + stalls); 
  DT(3, "*** VOPC_issued_trace: curr_iter=" << curr_vgpr_requests << ", total_iter=" << total_vgpr_requests << ", vlmul=" << vlmul_counter_ << ", " << *new_trace);
}



void VOpcUnit::vector_final_trace(instr_trace_t* trace) {

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
  total_vgpr_requests = 3;
  curr_vgpr_requests = 0;
  scalar_stalls = 0;
  vector_stalls = 0; 

  vl_counter_ = 0;
  vlmul_counter_ = 0; 
  curr_vlmul_counter = 0;

  instr_pending_ = false;
  lsu_flush_ = false;
  is_reduction_ = false;
}



uint32_t VOpcUnit::compute_vector_stalls(instr_trace_t* trace, uint32_t vlmul_index) {

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
      uint32_t bank_i = (trace->src_regs[i].idx + vlmul_index) % NUM_GPR_BANKS;
      uint32_t bank_j = (trace->src_regs[j].idx + vlmul_index) % NUM_GPR_BANKS;
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



uint32_t VOpcUnit::compute_total_vgpr_requests(instr_trace_t* trace) {

  // Calculate how many requests made to vrf

  // TODO : Modify this for mixed width precision
  uint32_t VL_count = VLEN/XLEN;
  uint32_t max_threads_per_req = 0;
  uint32_t vgpr_requests_per_thread = 0;
  uint32_t nz_iterator_req = 0;

  // Assuming SIMD_WIDTH, VL_count are powers of 2 
  // Case 1 : SIMD_WIDTH > VL_count 
  // ==> Each SIMD_WIDTH has 2 or more thread ==> Meaning NZ iterator works at granularity of SIMD_WIDTH / VL_count
  if(SIMD_WIDTH > VL_count) {
    max_threads_per_req = SIMD_WIDTH / VL_count;
    vgpr_requests_per_thread = 1;

    auto temp_tmask = trace->tmask;
    for(uint32_t tid_base = 0; tid_base < NUM_THREADS; tid_base += max_threads_per_req){
      int flag = 0;
        
      for(uint32_t tid_offset = 0; tid_offset < max_threads_per_req; tid_offset++) {
        if(temp_tmask.test(tid_base + tid_offset)){
          flag = 1;
        }
      }
      nz_iterator_req += flag;
    }
  }
  // Case 2 : SIMD_WIDTH < VL_count 
  // ==> Meaning each SIMD_WIDTH only has half a thread ==> Meaning Nz iterator can work at granularity of each thread
  else {
    max_threads_per_req = 1;
    vgpr_requests_per_thread = VL_count / SIMD_WIDTH;
    nz_iterator_req = trace->tmask.count();
  }

  uint32_t total_requests = (max_threads_per_req) * vgpr_requests_per_thread * nz_iterator_req;

  return total_requests;
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
  case VpuOpType::TENSOR:
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



void VOpcUnit::writeback(instr_trace_t* trace) {
  // only notify writeback for the currently active reduction instructions
  if (instr_pending_ && trace->PC == active_PC_) {
    DT(3, "VOPC_Writeback_Received" << ", " << *trace);
  }
}



/*****************************************/
// Notes on Reduction 
//
// Working example 
  /*
   * Consider: Layout of VRF bank 0 is [ (T0,T1), (T2,T3), ..... (T30,T31)] 
   * Then: 
   * Reduction Tree Level 0:
   *  Idea: 
   *  1. Get 2 VRF (T0,T1 | T2,T3) --> Send out 
   *  2. Get 2 VRF (T4,T5 | T6,T7) --> Send out 
   *  3. Continue to perform reduction on these 
   *
   *  Example below:
   *  CC0: VRF req  (T0,T1)
   *  CC1: VRF req  (T2,T3)                     
   *  CC2: Send out (T0,T1,T2,T3)     --> SIMD full (T0->T3), rsp buffer empty
   *  CC3: VRF req  (T4,T5)           --> SIMD (other insn) , rsp buffer 1/2 full (TO->T3) 
   *  CC4: VRF req  (T6,T7)           --> SIMD (other insn) , rsp buffer 1/2 full (TO->T3) 
   *  CC5: Send out (T4,T5,T6,T7)     --> SIMD full (T4->T7), rsp buffer 1/2 full (TO->T3)
   *  CC6: Wait until rsp buffer full --> SIMD (other insn) , rsp buffer full (TO->T7) 
   *
   * Reduction Tree Level 1 --> SIMD is now full
   *  CC0: Repeat same thing
   * ...Repeat until last level
   *
   * Start 2nd batch 
   * CC0: VRF req (T8, T9)
   * etc
   * ...Repeat until last level
   *
   */
  // Overall, optimally, it optimizes for 4 VRF req batches
  // Hence, need to consider 3 different caess, (Only 1 VRF req batch, Only 2 VRF req batch, Only 4)
  // Note, the total number of batches is assumed to be a power of 2

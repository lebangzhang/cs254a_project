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
#include "math.h"


# define VOPC_VECTOR_UOP_DELAY 3
# define VOPC_SCALAR_DELAY 2


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
  total_gpr_requests = 1;
  curr_vgpr_requests = 0;
  scalar_stalls = 0;
  vector_stalls = 0;

  // Counter variables
  curr_vector_timing_counter = 0;
  uops_vector_timing_counter = 0;

  // Reduction variables
  red_tree_height = 0;

  // True/False variables
  instr_pending_ = false;
  wb_rsp_received = false;
  lsu_flush_ = false;
  is_reduction_ = false;
  is_vset = false;
  done = false;
}



void VOpcUnit::tick() {

  // 0. Process incoming instructions
  if (Input.empty())
    return;
  auto trace = Input.front();

  // 1. First Entry initialization
  if(!instr_pending_) {

    // Calculate total gpr_requests to GPRF needed
    total_gpr_requests = compute_total_gpr_requests(trace);
    DT(3, "Scalar Requests: " << total_gpr_requests << " "<< *trace);

    // Calculate total vgpr_requests to VRF needed
    curr_vgpr_requests = compute_total_vgpr_requests(trace);
    DT(3, "Vector Requests: " << curr_vgpr_requests << " "<< *trace);

    // Calculate Expected RF stalls
    scalar_stalls = this->compute_scalar_stalls(trace);
    vector_stalls = this->compute_vector_stalls(trace);

    // Initialize vector counter for state machine
    // uops_vector_timing_counter = (Time for 1 vector uop)
    // curr_vector_timing_counter = (Time for 1 scalar) + (Time for 1 vector uop) + (Initial time to move from Idle)
    uops_vector_timing_counter = (vector_stalls + VOPC_VECTOR_UOP_DELAY);
    curr_vector_timing_counter = (scalar_stalls + VOPC_SCALAR_DELAY) + uops_vector_timing_counter + 1;


    // Check if vector insn -> Then compute Reduction parameters and VSET parameters
    if (trace->fu_type == FUType::VPU) {
      auto trace_data = std::dynamic_pointer_cast<VecUnit::ExeTraceData>(trace->data);
      auto vpu_op = trace_data->vpu_op;


      // Vset Parameters
      is_vset       = (vpu_op == VpuOpType::VSET);

      // Set parameters if vset insn
      if(is_vset){
        curr_vector_timing_counter = (scalar_stalls + VOPC_SCALAR_DELAY) + VOPC_VECTOR_UOP_DELAY;
      }


      // Redution Parameters
      is_reduction_ = (vpu_op >= VpuOpType::ARITH_R);

      // If not fused vpu
      #ifdef NOT_FUSED_VPU
        vector_stalls = 0;
        if(is_reduction_){
          uops_vector_timing_counter = VOPC_VECTOR_UOP_DELAY;
          curr_vector_timing_counter = (scalar_stalls + VOPC_SCALAR_DELAY) + VOPC_VECTOR_UOP_DELAY;

          // TOFIX : Hacky Temporary Fix (Only applies for 'fully vector')
          curr_vgpr_requests = NUM_THREADS;
        }
        is_reduction_ = false;  // Fallback to stadnard insn timing
      #endif

      active_PC_ = trace->PC;

      // Set parameters if reduction insn
      if(is_reduction_){
        DT(3, "*** VOPC_is_reduce"  << " Trace: "  << *trace);

        red_tree_height = this->tree_height(trace_data->vl);

        // Special case when vl = 1  ==> Handle reduction like std insn
        // I.e: vl = 1 ==> Implies similar to vadd
        if(trace_data->vl == 1){
          is_reduction_ = false;
        }

        // Set reduction tree height to 0
        curr_red_tree_h = 0;

        // Recompute current timing for fused
        vector_stalls = 1;
        curr_vector_timing_counter = (scalar_stalls + VOPC_SCALAR_DELAY) + VOPC_VECTOR_UOP_DELAY + 1;

        // Fetch 2x VRF for layer 0
        if(curr_vgpr_requests >= 2){

          // +2 to handle the fetch and response
          curr_vector_timing_counter += (vector_stalls);
        }


        curr_red_tree_h = 0;
        wb_rsp_received = true;
      }
    }

    // Remove first entry in next CC
    instr_pending_ = true;

    // Not yet EOP
    done = false;
  }


  // Scalar instruction : Pipelined Timing
  if(trace->fu_type != FUType::VPU) {

    // Issue all except last trace in pipelined manner
    // Note: Shouldn't affect timing in most scenarios because only 1 gpr_requests being sent
    uint32_t i = 0;
    for(i = 0; i < (total_gpr_requests-1); i++){

      auto trace_alloc = core_->trace_pool().allocate(1);
      auto new_trace = new (trace_alloc) instr_trace_t(*trace);
      new_trace->wb = false; // disable scoreboard release

      // Issue Trace to Execution
      this->Output.push(new_trace, VOPC_SCALAR_DELAY + scalar_stalls + i);
      DT(3, "*** VOPC_scalar_trace: Offset: " << VOPC_SCALAR_DELAY + scalar_stalls + i << " Trace: "  << *trace);
    }

    // Last trace in pipelined manner
    this->Output.push(trace, VOPC_SCALAR_DELAY + scalar_stalls + i);
    DT(3, "*** VOPC_scalar_trace: " << *trace);
    Input.pop();

    // Set instruction as done
    done = true;


  // Vector instruction : State Machine Timing
  } else {

    // Is a vset insn
    if(is_vset){

      // Decrement counter each CC
      if(curr_vector_timing_counter > 0){
        curr_vector_timing_counter -= 1;
      }

      // When counter reaches 0 ==> Send a trace
      if(curr_vector_timing_counter == 0){
          this->send_last_trace(trace);
      }



    // Standard Instruction
    // Access GPR for 1 chunk
    // Assume: Since subsequent GPR Chunk are concurrent with sending vector uops
    // ==> Assume (time for GPR) < (time for uops) ==> Hence, only consider first gpr_requests,
    //      Other gpr_requests would be ready during time vector uops are sent
    } else if(!is_reduction_){

      // Decrement counter each CC
      if(curr_vector_timing_counter > 0){
        curr_vector_timing_counter -= 1;
      }

      // When counter reaches 0 ==> Send a trace
      if(curr_vector_timing_counter == 0){

        // TOFIX: Add the SEW Loop inside

        // Check if there is still a uop to send
        if(curr_vgpr_requests-1 > 0) {

          // Send uop to execution
          this->send_uop_trace(trace);

          // Decrement vgpr requests
          curr_vgpr_requests -= 1;

        // Send the last trace
        } else {
          this->send_last_trace(trace);
        }

        // Reset curr_vector_timing_counter for the next height
        curr_vector_timing_counter = uops_vector_timing_counter;
      }


    // Is a reduction instruction
    // and fused architecture
    } else {

      // Case 1: First Layer of reduction Tree
      if(curr_red_tree_h == 0){

        // Decrement counter each CC
        // ==> Don't wait for writeback
        if(curr_vector_timing_counter > 0){
          curr_vector_timing_counter -= 1;
        }

        // When counter reaches 0 ==> Send a trace
        if(curr_vector_timing_counter == 0){

          // TOFIX: Add the SEW Loop inside

          // Case 1a: Last trace to send out
          if((curr_vgpr_requests == 0) and (red_tree_height == 1)){
            this->send_last_trace(trace);

          // Case 1b: Other traces to send out
          } else {
            // Send uop to execution
            this->send_uop_trace(trace);

            // Set WB hasn't been received
            wb_rsp_received = false;

            // Decrement vgpr requests
            if(curr_vgpr_requests >= 2){
              curr_vgpr_requests -= 2;
            } else {
              curr_vgpr_requests -= 1;
            }

            // Next height
            curr_red_tree_h = (curr_red_tree_h + 1) % (red_tree_height);

            // Reset curr_vector_timing_counter for the next height
            this->compute_red_vector_timing_counter(curr_red_tree_h);
          }
        }

      // Case 2: Subsequent layers of reduction tree
      // Need to wait for writeback
      } else {

        // Writeback Received --> Start processing state machine
        if(wb_rsp_received){

          // Decrement counter each CC
          if(curr_vector_timing_counter > 0){
            curr_vector_timing_counter -= 1;
          }

          // When counter reaches 0 ==> Send a trace
          if(curr_vector_timing_counter == 0){

            // Case 2a : Last trace to send out
            if((curr_vgpr_requests == 0) and (curr_red_tree_h == red_tree_height - 1) ){
              this->send_last_trace(trace);

            // Case 2b : Other traces to send out
            } else {

              // Send uop to execution
              this->send_uop_trace(trace);

              // Set WB hasn't been received
              wb_rsp_received = false;

              // Next height
              curr_red_tree_h = (curr_red_tree_h + 1) % (red_tree_height);

              // Reset curr_vector_timing_counter for the next height
              this->compute_red_vector_timing_counter(curr_red_tree_h);
            }
          }
        }
      }
    }
  }

  // Reset parameters for next instruction
  if(done){
    instr_pending_ = false;
    wb_rsp_received = false;
    lsu_flush_ = false;
    is_reduction_ = false;
    is_vset = false;
    done = false;
  }

}

uint32_t VOpcUnit::tree_height(uint32_t n){

  if(n == 1){
    return 1;
  }

  return ceil(log2(n));
}

void VOpcUnit::compute_red_vector_timing_counter(uint32_t red_tree_h){

  // Case 1: Next height is '0' (new chunk)
  // ==> Start collecting registers, dont wait for wb
  if(red_tree_h == 0){

    curr_vector_timing_counter = VOPC_VECTOR_UOP_DELAY;

    // If can fetch 2, then fetch 2 registers
    if(curr_vgpr_requests >= 2){
      curr_vector_timing_counter += (1);
    }

  // Case 2: Next height is part of the tree
  // Don't fetch VRF again
  } else {
    curr_vector_timing_counter = VOPC_VECTOR_UOP_DELAY - 1;
  }

}


void VOpcUnit::send_last_trace(instr_trace_t* trace) {

  this->Output.push(trace);
  DT(3, "*** VOPC_final_vector_trace: " << *trace);
  Input.pop();

  // Set instruction as done
  done = true;
}


void VOpcUnit::send_uop_trace(instr_trace_t* trace) {

  auto trace_alloc = core_->trace_pool().allocate(1);
  auto new_trace = new (trace_alloc) instr_trace_t(*trace);

  // Check if is LSU instruction
  this->lsu_flush(new_trace);

  // Add decode
  if (trace->fu_type == FUType::VPU) {
    // translate VPU instructions
    this->translate(new_trace);
  }

  // Disable scoreboard release
  new_trace->wb = false;

  // Issue Trace to Execution
  this->Output.push(new_trace);
  DT(3, "*** VOPC_vector_trace: Time: (Look at clock); Trace: "  << *trace);

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
      uint32_t bank_i = (trace->src_regs[i].idx) % NUM_GPR_BANKS;
      uint32_t bank_j = (trace->src_regs[j].idx) % NUM_GPR_BANKS;
      if (bank_i == bank_j) {

        // Check if vector type
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

        // Check if not vector type
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

uint32_t VOpcUnit::compute_total_gpr_requests(instr_trace_t* trace) {

  // Calculate how many requests made to rf

  uint32_t total_requests = 0;

  // Case: All threads fit into 1 chunk
  if(SIMD_WIDTH >= NUM_THREADS)
    return 1;

  // Case: Threads are spread across chunks
  auto temp_tmask = trace->tmask;

  uint32_t flag = 0;
  for(uint32_t tid = 0; tid < NUM_THREADS; tid++){

    // Update flag if thread is active
    if(temp_tmask.test(tid))
      flag = 1;

    // Increment at SIMD_WIDTH granularity + Reset flag for next chunk
    if( (tid % SIMD_WIDTH) == 0){
      if(flag)
        total_requests += 1;
      flag = 0;
    }
  }

  // Temporary Assumption that SIMD_WIDTH >= NUM_THREADS

  /*return total_requests;*/
  return 1;
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
    DT(1, "Error: VOPC translation - " << *trace);
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

  /*printf("----------\n");*/
  /*printf("%x %x\n", trace->PC ,active_PC_);*/
  /*printf("%d\n", trace->wb ,active_PC_);*/
  /*printf("----------\n");*/

  // only notify writeback for the currently active reduction instructions
  if (instr_pending_ && trace->PC == active_PC_) {
    DT(3, "VOPC_Writeback_Received" << ", " << *trace);

    // Set response received as true
    wb_rsp_received = true;
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



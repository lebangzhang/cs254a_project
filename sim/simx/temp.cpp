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

using namespace vortex;

VOpcUnit::VOpcUnit(const SimContext &ctx, Core* core)
  : SimObject<VOpcUnit>(ctx, "vopc-unit")
  , Input(this, 1)
  , Output(this)
  , gpr_req_ports(this)
  , gpr_rsp_ports(this)
  , vgpr_req_ports(this)
  , vgpr_rsp_ports(this)
  , core_(core) {
  this->reset();
}

VOpcUnit::~VOpcUnit() {}

void VOpcUnit::reset() {
  pending_s_rsps_ = 0;
  pending_v_rsps_ = 0;
  vl_counter_ = 0;
  vlmul_counter_ = 0;
  red_counter_ = 0;
  wb_counter_ = 0;
  instr_pending_ = false;
  is_reduction_ = false;
  lsu_flush_ = false;
  total_stalls_ = 0;
  total_vgpr_requests = 0;
  total_vgpr_req_count = 0;
  vlmul_index = 0;
  red_level = 0;
  red_vrf_rsp = 0;
  red_trace_count = 0;
  red_trace_flag = 0;
  red_state = 0; 
  fused_next_red_state = 1;
}

void VOpcUnit::tick() {
  // process incoming instructions
  if (Input.empty())
    return;
  auto trace = Input.front();

  if (!instr_pending_) {
    // calculate operands to fetch
    std::bitset<NUM_SRC_REGS> sopd_to_fetch;
    assert(pending_s_rsps_ == 0);
    assert(pending_v_rsps_ == 0);

    // capture SIMD counters
    if (trace->fu_type == FUType::VPU) {
      auto trace_data = std::dynamic_pointer_cast<VecUnit::ExeTraceData>(trace->data);
      active_PC_ = trace->PC;
      if (trace->vpu_type != VpuType::VSET) {
        vl_counter_ = trace_data->vl;
        vlmul_counter_ = trace_data->vlmul;
        vlmul_index = 0;
      } else {
        vl_counter_ = 1;
        vlmul_counter_ = 1;
        vlmul_index = 0;
      }
      is_reduction_ = (trace->vpu_type >= VpuType::ARITH_R);
      if (is_reduction_) {

        // Compute red_counter_
        // red_counter_ : Number of tree layers needed 
        uint32_t VL_count = VLEN/XLEN;  // We assumed wasting the regfile bw
        red_counter_ = 0;
        assert(VL_count > 0);
        red_counter_ = ceil(log2( VL_count));

        // Debug Prints
        // DT(3, "*** VOPC begin: is_red=" << ", VL_count=" << VL_count << ", " << *trace);
        // DT(3, "*** VOPC begin: is_red=" << ", log2    =" << log2(VL_count) << ", " << *trace);
        // DT(3, "*** VOPC begin: is_red=" << ", ceil    =" << ceil(log2(VL_count)) << ", " << *trace);

        /*wb_counter_ = (red_counter_ > 1) ? (red_counter_ - 1) : 0;*/
        red_level = 0;
        red_state = 0;
        red_vrf_rsp = 0;
        red_trace_count = 0;
        red_trace_flag = 0;
        fused_next_red_state = 1;
      }
    } else {
      assert(trace->fu_type == FUType::LSU);
      auto trace_data = std::dynamic_pointer_cast<VecUnit::MemTraceData>(trace->data);
      vs2_opd_ = (trace->src_regs[1].type != RegType::None) ? 1 : -0;
      vl_counter_ = trace_data->vl;
      vlmul_counter_ = trace_data->vnf;
    }

    assert(vlmul_counter_ != 0);

    // HELPS **** Do we still need a vl_counter
    if (vl_counter_ == 0) {
      // Convert to Nop
      trace->fu_type = FUType::ALU;
      trace->alu_type = AluType::ARITH;
      this->Output.push(trace);
      Input.pop();
      return;
    }
    DT(3, "*** VOPC begin: vl=" << vl_counter_ << ", vlmul=" << vlmul_counter_ << ", " << *trace);

    // gather operands to fetch
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
        if (trace->src_regs[i].type == RegType::Vector) {
          vopd_to_fetch_.set(i);
        } else {
          sopd_to_fetch.set(i);
        }
      }
    }

    // send GPR requests (we do this once)
    for (uint32_t i = 0; i < NUM_SRC_REGS; i++) {
      if (sopd_to_fetch.test(i)) {
        GprReq gpr_req;
        gpr_req.rid = trace->src_regs[i].id();
        gpr_req.wid = trace->wid;
        gpr_req.opd = i;
        gpr_req_ports.push(gpr_req);
        ++pending_s_rsps_;
      }
    }


    // Calculate how many requests made to vrf
    uint32_t VL_count = VLEN/XLEN;
    uint32_t max_threads_per_req = 0;
    uint32_t iterations_per_thread = 0;
    uint32_t nz_iterator_req = 0;

    // Assuming SIMD_WIDTH, VL_count are powers of 2 
    // Case 1 : SIMD_WIDTH > VL_count 
    // ==> Each SIMD_WIDTH has 2 or more thread
    // ==> Meaning NZ iterator works at granularity of SIMD_WIDTH / VL_count
    if(SIMD_WIDTH > VL_count) {
      max_threads_per_req = SIMD_WIDTH / VL_count;
      iterations_per_thread = 1;

      // TODO : Probably exists a more elegant way of doing this calculation
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
    // ==> Meaning each SIMD_WIDTH only has half a thread 
    // ==> Meaning Nz iterator can work at granularity of each thread
    else {
      max_threads_per_req = 1;
      iterations_per_thread = VL_count / SIMD_WIDTH;
      nz_iterator_req = trace->tmask.count();
    }
    total_vgpr_requests = (max_threads_per_req) * iterations_per_thread * nz_iterator_req;
    total_vgpr_req_count = total_vgpr_requests;


    // send VGPR requests (we do this once)
    for (uint32_t i = 0; i < NUM_SRC_REGS; i++) {
      if (vopd_to_fetch_.test(i)) {
        VgprReq vgpr_req;
        vgpr_req.rid = trace->src_regs[i].id();
        vgpr_req.wid = trace->wid;
        vgpr_req.opd = i;
        vgpr_req_ports.push(vgpr_req);
        ++pending_v_rsps_;
      }
    }
    total_vgpr_req_count -= 1;

    // mark current instruction as pending
    instr_pending_ = true;
  }


  // Debug prints
  /*DT(3, "*** VOPC running: vl=" << vl_counter_ << ", vlmul=" << vlmul_counter_ << ", " << *trace);*/
  /*DT(3, "*** VOPC running: pending_s=" << pending_s_rsps_ << ", pending_v=" << pending_v_rsps_ << ", " << *trace);*/
  /*DT(3, "*** VOPC running: total_vgpr=" << total_vgpr_req_count << ", is_reduction_" << is_reduction_ << ", " << *trace);*/
  /*DT(3, "*** VOPC running: Condition: " << (0 == pending_s_rsps_) << ":" << (pending_v_rsps_ == 0) << ":" <<  ((total_vgpr_req_count == 0) && (!is_reduction_)) <<", " << *trace);*/

  // process incoming GPR responses
  if (!gpr_rsp_ports.empty()) {
    assert(pending_s_rsps_ != 0);
    --pending_s_rsps_;
    auto rsp = gpr_rsp_ports.front();
    __unused(rsp);
    gpr_rsp_ports.pop();
  }

  // process incoming VGPR responses
  if (!vgpr_rsp_ports.empty()) {
    assert(pending_v_rsps_ != 0);
    --pending_v_rsps_;
    auto rsp = vgpr_rsp_ports.front();
    __unused(rsp);
    vgpr_rsp_ports.pop();
  }

  // For Non-Writeback instructions 
  if(!is_reduction_) {
    // Send the next batch of requests
    if( (total_vgpr_req_count != 0) && (pending_v_rsps_ == 0) ){
      for (uint32_t i = 0; i < NUM_SRC_REGS; i++) {
        if (vopd_to_fetch_.test(i)) {
          VgprReq vgpr_req;
          vgpr_req.rid = trace->src_regs[i].id();
          vgpr_req.wid = trace->wid;
          vgpr_req.opd = i;
          vgpr_req_ports.push(vgpr_req);
          ++pending_v_rsps_;
        }
      }
      total_vgpr_req_count -= 1;
    }
  }

  // process outgoing instructions
  // For the case of no reduction, only process once finished
  // TOFIX: there is definitely a better way to write this, for now assume compiler is smart :) 
  int flag = 0;
  if(!is_reduction_){
    if(total_vgpr_req_count == 0){
        flag = 1;
    }
  } else {
    flag = 1;
  }

  if ( (0 == pending_s_rsps_) && (pending_v_rsps_ == 0) && (flag)){ 

    auto trace = Input.front();
    bool done = false;


  #ifdef FUSED_VPU
    // Reduction handled below
    done = this->fused_schedule(trace);
  #else
    done = this->schedule(trace);
  #endif
    if (done) {
      // release instruction
      Input.pop();
      // reset states
      instr_pending_ = false;
      is_reduction_ = false;
      lsu_flush_ = false;
      red_counter_ = 0;
      wb_counter_ = 0;
      vlmul_index = 0; 
      total_vgpr_req_count = 0; 
      total_vgpr_requests = 0;
      red_level = 0; 
      red_state = 0;
      red_vrf_rsp = 0;
      red_trace_count = 0;
      red_trace_flag = 0;
      fused_next_red_state = 1;
    }
  }
}

bool VOpcUnit::schedule(instr_trace_t* trace) {

  // we need to run the instruction again for vlmul
  assert(vlmul_counter_ > 0);
  --vlmul_counter_;
  
  // Initialize the number of vgpr requests and vlmul index 
  total_vgpr_req_count = total_vgpr_requests;
  vlmul_index += 1;
  
  // Start the vlmul counter
  if (vlmul_counter_ != 0) {
    // fetch the vector operands again (skip vs2 operand for LD/ST)
    for (uint32_t i = 0; i < NUM_SRC_REGS; i++) {
      if (vopd_to_fetch_.test(i) && vs2_opd_ != i) {
        VgprReq vgpr_req;
        vgpr_req.rid = trace->src_regs[i].id() + vlmul_index;
        vgpr_req.wid = trace->wid;
        vgpr_req.opd = i;
        vgpr_req_ports.push(vgpr_req);
        ++pending_v_rsps_;
      }
    }
    total_vgpr_req_count -= 1;

    // issue a cloned instruction trace
    auto trace_alloc = core_->trace_pool().allocate(1);
    auto new_trace = new (trace_alloc) instr_trace_t(*trace);
    new_trace->wb = false; // disable scoreboard release
    this->lsu_flush(new_trace);
    DT(3, "*** VOPC next group: vlmul=" << vlmul_counter_ << ", " << *new_trace);
    this->Output.push(new_trace);
    return false;
  }
  // we are done with all iterations, issue the original instruction
  this->lsu_flush(trace);
  DT(3, "*** VOPC done: " << *trace);
  this->Output.push(trace);
  return true;
}


// Handles on a per vlmul basis
void VOpcUnit::reduction_layer0_1_group(instr_trace_t* trace, uint32_t vl_index) {

  // (Enter Function Condition)
  if(red_state != 0){
    return;
  }


  DT(3, "*** VOPC reduction, layer0_1_group: vlmul=" << vlmul_counter_ << ", red_state=" << red_state << ", total_vgpr_req_count="<< total_vgpr_req_count << ", " << *trace);
  assert(red_state == 0);

  // (Response Condition) Reduction Writeback Response
  // Note: red_trace_count updated in the writeback function
  if(red_trace_count == 0){
    red_trace_flag = 1;
  }

  // (Terminating Condition) Enter here if all VRF requests and responses have been made
  if( (red_level != 0) ) { 
    // Layer 0 finished --> Move to next state
    if(red_trace_flag){  // Technically, can just check red_trace_count, more readable
      red_state += 1;
    }
    return;
  }


  // (Request Condition) Reduction request:
  // Only fetch registers when we are at level 0 for this batch
  // Keep sending requests because the response buffer can store 4 requests 
  if(red_level == 0){

    // Check if all responses for 1 batch has been received
    if(pending_v_rsps_ == 0){
      red_vrf_rsp += 1;
    }
    
    // When the 4th response is received, stop sending requests
    if(red_vrf_rsp < 4){
      // Send the next batch of VRF requests
      if( (total_vgpr_req_count != 0) && (pending_v_rsps_ == 0) ){    
        for (uint32_t i = 0; i < NUM_SRC_REGS; i++) {
          if (vopd_to_fetch_.test(i)) {
            VgprReq vgpr_req;
            vgpr_req.rid = trace->src_regs[i].id() + vl_index;
            vgpr_req.wid = trace->wid;
            vgpr_req.opd = i;
            vgpr_req_ports.push(vgpr_req);
            ++pending_v_rsps_;
          }
        }

        // Only decrement req count for first vl 
        // Because we iterate vl first then the vgpr req count 
        if(vl_index == 0){
          total_vgpr_req_count -= 1;
        }
      }
    }
  }

  // When Every 2 VRF Responses has been received --> Dispatch to SIMD
  if(red_vrf_rsp % 2 == 0){
    // issue a cloned instruction trace
    auto trace_alloc = core_->trace_pool().allocate(1);
    auto new_trace = new (trace_alloc) instr_trace_t(*trace);
    new_trace->wb = false; // disable scoreboard release
    this->decode(new_trace);
    DT(3, "*** Reduction sent " << ", Level = " << red_level << ", " << *new_trace);
    this->Output.push(new_trace);

    // Count number of traces been issued
    red_trace_count += 1;
  }

  // When 4 VRF Responses have been received --> Switch to next level and stop VRF requests
  if(red_vrf_rsp == 4){
     red_level += 1;
  
  } else if ((total_vgpr_req_count == 0) && (red_vrf_rsp %2 == 0) ){ // Alternatively, no more VRF left requests
     red_level += 1; 
  }
}


bool VOpcUnit::reduction_layer1_and_more(instr_trace_t* trace) {

  // (Enter Function Condition)
  if(red_state == 0){
    return false;
  }

  // Debug 
  /*auto trace_data = std::dynamic_pointer_cast<VecUnit::ExeTraceData>(trace->data);*/
  /*DT(3, "*** VOPC reduction, layer1_and_more: vl_counter=" << vl_counter_ << ", vlmul=" << vlmul_counter_ << ", red_state=" << red_state << ", " << *trace);*/
  /*DT(3, "*** VOPC reduction, layer1_and_more: trace_vl=" << trace_data->vl << ", trace_vlmul=" << trace_data->vlmul << ", " << *trace);*/
  DT(3, "*** VOPC reduction, layer1_and_more: red_level=" << red_level << ", red_counter=" << red_counter_ << ", red_trace_count"<< red_trace_count <<", " << *trace);
  assert(red_state != 0);


  // (Response Condition) Receive Trace Response
  // Note: red_trace_count is  
  if(red_trace_count != 0){
    return false;
  } 

  // (Terminating Condition) Check if finished operation
  if(red_level == red_counter_){
    return true;
  } 

  // (Request Condition) Send Trace Request  
  auto trace_alloc = core_->trace_pool().allocate(1);
  auto new_trace = new (trace_alloc) instr_trace_t(*trace);
  new_trace->wb = false; // disable scoreboard release
  this->decode(new_trace);
  DT(3, "*** Reduction sent " << ", Level = " << red_level << ", " << *new_trace);
  this->Output.push(new_trace);

  red_trace_count += 1;
  red_level += 1;

  return false;
}


bool VOpcUnit::fused_schedule(instr_trace_t* trace) {

  // Value to return
  bool finished = false;

  // Perform Reduction and vlmul iteration
  if(is_reduction_){


      // State 1: Performing the Reduction
      if(fused_next_red_state == 1){
        /*DT(3, "*** VOPC reduction, State 1" << ", vlmul_counter=" << vlmul_counter_ << ", total_vgpr_req_count"<< total_vgpr_req_count <<", " << *trace);*/
        // 1. Perform reduce layer 0 
        reduction_layer0_1_group(trace, vlmul_index); 
        // 2. Reduced the current group 
        finished = reduction_layer1_and_more(trace);
      }

      // State 0: Initial Issue of vgpr requests 
      // To "simulate" the first requests when instr_pending = true
      if(fused_next_red_state == 0){

        /*DT(3, "*** VOPC reduction, State 0" << ", vlmul_counter=" << vlmul_counter_ << ", total_vgpr_req_count"<< total_vgpr_req_count <<", " << *trace);*/

        // Issue vgpr requests
        // fetch the vector operands again (skip vs2 operand for LD/ST)
        for (uint32_t i = 0; i < NUM_SRC_REGS; i++) {
          if (vopd_to_fetch_.test(i) && vs2_opd_ != i) {
            VgprReq vgpr_req;
            vgpr_req.rid = trace->src_regs[i].id() + vlmul_index;
            vgpr_req.wid = trace->wid;
            vgpr_req.opd = i;
            vgpr_req_ports.push(vgpr_req);
            ++pending_v_rsps_;
          }
        }

        // Move on to next reduction state in next CC
        fused_next_red_state = 1;
      }

      // *** Update Logic to prepare next CC ***

      // Performs update for vl 
      // Decrement vl and goto State 1 in the next CC 
      if(finished && (vlmul_counter_ != 0)){

        DT(3, "*** VOPC reduction update, Update vlmul_counter" << ", curr_vlmul_counter=" << vlmul_counter_ << ", " << *trace);

        assert(vlmul_counter_ > 0);
        --vlmul_counter_;
  
        // Initialize vlmul index 
        vlmul_index += 1;

        // Reset reduction parameters 
        red_level = 0;
        red_state = 0;
        red_vrf_rsp = 0;
        red_trace_count = 0;
        red_trace_flag = 0;

        // Jump to state 0 in next CC
        fused_next_red_state = 0;

        return false;
      }


      // Performs update for total_vgpr_req_count 
      // Repeat until all total_vgpr_req_count is finished 
      // Note: decrement of total_vgpr_req_count is done by the reduction (State 1) 
      if(finished && (vlmul_counter_ == 0) && (total_vgpr_req_count != 0)){

        DT(3, "*** VOPC reduction update, Update vgpr_counter" << ", curr_total_vgpr_req_count=" << total_vgpr_req_count << ", " << *trace);

        // Reset reduction parameters 
        red_level = 0;
        red_state = 0;
        red_vrf_rsp = 0;
        red_trace_count = 0;
        red_trace_flag = 0;

        // Reset vl parameters
        auto trace_data = std::dynamic_pointer_cast<VecUnit::ExeTraceData>(trace->data);
        vlmul_index = 0;
        vlmul_counter_ = trace_data->vlmul;

        // Jump to state 0 in next CC 
        fused_next_red_state = 0;

        return false;
      }


      // Final update if conditions are fufilled
      if(finished && (vlmul_counter_ == 0) && (total_vgpr_req_count == 0)) {
        // we are done with all iterations, issue the original instruction
        this->decode(trace);
        DT(3, "*** VOPC done: " << *trace);
        this->Output.push(trace);
        return true;
      }


  } else {
      // Perform vlmul iteration
      finished = this->schedule(trace);
      return finished;
  }

  // Shouldn't reach here 
  return finished;
}





void VOpcUnit::decode(instr_trace_t* trace) {
  // translate to scalar pipeline
  switch (trace->fu_type) {
  case FUType::LSU:
    // no conversion
    break;
  case FUType::VPU:
    // decode VPU instructions
    switch (trace->vpu_type) {
    case VpuType::VSET:
      // no convertion
      break;
    case VpuType::ARITH:
    case VpuType::ARITH_R:
      trace->fu_type = FUType::ALU;
      trace->alu_type = AluType::ARITH;
      break;
    case VpuType::IMUL:
      trace->fu_type = FUType::ALU;
      trace->alu_type = AluType::IMUL;
      break;
    case VpuType::IDIV:
      trace->fu_type = FUType::ALU;
      trace->alu_type = AluType::IDIV;
      break;
    case VpuType::FMA:
    case VpuType::FMA_R:
      trace->fu_type = FUType::FPU;
      trace->fpu_type = FpuType::FMA;
      break;
    case VpuType::FDIV:
      trace->fu_type = FUType::FPU;
      trace->fpu_type = FpuType::FDIV;
      break;
    case VpuType::FSQRT:
      trace->fu_type = FUType::FPU;
      trace->fpu_type = FpuType::FSQRT;
      break;
    case VpuType::FCVT:
      trace->fu_type = FUType::FPU;
      trace->fpu_type = FpuType::FCVT;
      break;
    case VpuType::FNCP:
    case VpuType::FNCP_R:
      trace->fu_type = FUType::FPU;
      trace->fpu_type = FpuType::FNCP;
      break;
    default:
      assert(false);
    }
    break;
  default:
    assert(false);
  }

  this->lsu_flush(trace);
}

void VOpcUnit::writeback(instr_trace_t* trace) {
  // only notify writeback for the currently active reduction instructions
  if (instr_pending_ && wb_counter_ > 0 && trace->PC == active_PC_) {
    --wb_counter_;
  }

  // For reduction layer 0 --> notify 
  if (instr_pending_ && red_trace_count > 0 && trace->PC == active_PC_) {
    --red_trace_count;
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




/*****************************************/
// Notes on Reduction 
//
// Working example 
  /*
   * Consider: Layout of VRF bank 0 is [ (T0,T1), (T2,T3), ..... (T30,T31)] 
   * Then: 
   * Reduction Tree Level 0:
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



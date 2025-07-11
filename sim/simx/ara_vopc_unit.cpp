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

#include "ara_vopc_unit.h"
#include "core.h"

using namespace vortex;

Ara_VOpc_Unit::Ara_VOpc_Unit(const SimContext &ctx, Core* core)
  : SimObject<Ara_VOpc_Unit>(ctx, "ara-opc-unit")
  , Input(this, 1)
  , Output(this)
  , gpr_req_ports(this)
  , gpr_rsp_ports(this)
  , vgpr_req_ports(this)
  , vgpr_rsp_ports(this)
  , core_(core) {
  this->reset();
}

Ara_VOpc_Unit::~Ara_VOpc_Unit() {}

void Ara_VOpc_Unit::reset() {
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
}

void Ara_VOpc_Unit::tick() {
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
    if (std::get_if<VopType>(&trace->op_type)
     || std::get_if<VsetType>(&trace->op_type)) {
      auto trace_data = std::dynamic_pointer_cast<VecUnit::ExeTraceData>(trace->data);
      active_PC_ = trace->PC;
      if (trace_data->vpu_op != VpuOpType::VSET) {
        vl_counter_ = trace_data->vl;
        vlmul_counter_ = trace_data->vlmul;
      } else {
        vl_counter_ = 1;
        vlmul_counter_ = 1;
      }
      is_reduction_ = (trace_data->vpu_op >= VpuOpType::ARITH_R);
      if (is_reduction_) {
        red_counter_ = (vlmul_counter_ * vl_counter_) - 1;
        wb_counter_ = (red_counter_ > 1) ? (red_counter_ - 1) : 0;
      }
    } else {
      assert(trace->fu_type == FUType::LSU);
      auto trace_data = std::dynamic_pointer_cast<VecUnit::MemTraceData>(trace->data);
      vs2_opd_ = (trace->src_regs[1].type != RegType::None) ? 1 : -0;
      vl_counter_ = trace_data->vl;
      vlmul_counter_ = trace_data->vnf;
    }

    assert(vlmul_counter_ != 0);
    if (vl_counter_ == 0) {
      // Convert to Nop
      trace->fu_type = FUType::ALU;
      trace->op_type = AluType::ADD;
      this->Output.push(trace);
      Input.pop();
      return;
    }

    DT(4, "*** VOPC begin: vl=" << vl_counter_ << ", vlmul=" << vlmul_counter_ << ", " << *trace);

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


    // ARA 2 NOTE : Force the vgpr request to be 0
    total_vgpr_requests = 0;


    // mark current instruction as pending
    instr_pending_ = true;
  }

  // process incoming GPR responses
  if (!gpr_rsp_ports.empty()) {
    assert(pending_s_rsps_ != 0);
    --pending_s_rsps_;
    auto rsp = gpr_rsp_ports.front();
    __unused(rsp);
    gpr_rsp_ports.pop();
  }


  // process outgoing instructions
  if ( (0 == pending_s_rsps_) && (pending_v_rsps_ == 0) && (total_vgpr_requests == 0)) {
    auto trace = Input.front();
    bool done = false;


    // TOFIX_ARA : Check here
    // Ara 2 Note : Don't perform vlmul latency here
    vlmul_counter_ = 1;
    vl_counter_ = 1;

  // TOFIX_ARA : Need to fix the total_vgpr_requests case for vlmul
  #ifdef FUSED_Ara
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
    }
  }
}

bool Ara_VOpc_Unit::schedule(instr_trace_t* trace) {
  // we need to run the instruction again for vlmul
  assert(vlmul_counter_ > 0);
  --vlmul_counter_;
  if (vlmul_counter_ != 0) {
    // fetch the vector operands again (skip vs2 operand for LD/ST)
    for (uint32_t i = 0; i < NUM_SRC_REGS; i++) {
      if (vopd_to_fetch_.test(i) && vs2_opd_ != i) {
        VgprReq vgpr_req;
        vgpr_req.rid = trace->src_regs[i].id();
        vgpr_req.wid = trace->wid;
        vgpr_req.opd = i;
        vgpr_req_ports.push(vgpr_req);
        ++pending_v_rsps_;
      }
    }
    // issue a cloned instruction trace
    auto trace_alloc = core_->trace_pool().allocate(1);
    auto new_trace = new (trace_alloc) instr_trace_t(*trace);
    new_trace->wb = false; // disable scoreboard release
    this->lsu_flush(new_trace);
    DT(4, "*** VOPC next group: vlmul=" << vlmul_counter_ << ", " << *new_trace);
    this->Output.push(new_trace);
    return false;
  }
  // we are done with all iterations, issue the original instruction
  this->lsu_flush(trace);
  DT(4, "*** VOPC done: " << *trace);
  this->Output.push(trace);
  return true;
}

bool Ara_VOpc_Unit::fused_schedule(instr_trace_t* trace) {
  // reduction instructions are serialized via writeback
  if (is_reduction_) {
    if (red_counter_ == 0) {
      // wait on writeback
      if (wb_counter_ != 0)
        return false;
      // we are done with all iterations, issue the original instruction
      this->decode(trace);
      DT(4, "*** VOPC done: " << *trace);
      this->Output.push(trace);
      return true;
    } else {
      --red_counter_;
    }
  }

  // we need to run the instruction again for vlmul
  assert(vlmul_counter_ > 0);
  --vlmul_counter_;
  if (vlmul_counter_ != 0) {
    // fetch the vector operands again (skip vs2 operand for LD/ST)
    for (uint32_t i = 0; i < NUM_SRC_REGS; i++) {
      if (vopd_to_fetch_.test(i) && vs2_opd_ != i) {
        VgprReq vgpr_req;
        vgpr_req.rid = trace->src_regs[i].id();
        vgpr_req.wid = trace->wid;
        vgpr_req.opd = i;
        vgpr_req_ports.push(vgpr_req);
        ++pending_v_rsps_;
      }
    }

    if (is_reduction_ && red_counter_ == 0)
      return false; // we will issue the last trace next

    // issue a cloned instruction trace
    auto trace_alloc = core_->trace_pool().allocate(1);
    auto new_trace = new (trace_alloc) instr_trace_t(*trace);
    new_trace->wb = false; // disable scoreboard release
    this->decode(new_trace);
    DT(4, "*** VOPC next group: vlmul=" << vlmul_counter_ << ", " << *new_trace);
    this->Output.push(new_trace);
    return false;
  }

  // we need to run the instruction again for each lane
  assert(vl_counter_ > 0);
  --vl_counter_;
  if (vl_counter_ != 0) {
    // fetch the vector operands again (skip vs2 operand for LD/ST)
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
    // reset group counter
    if (trace->fu_type == FUType::VPU) {
      auto trace_data = std::dynamic_pointer_cast<VecUnit::ExeTraceData>(trace->data);
      vlmul_counter_ = trace_data->vlmul;
    } else {
      assert(trace->fu_type == FUType::LSU);
      auto trace_data = std::dynamic_pointer_cast<VecUnit::MemTraceData>(trace->data);
      vlmul_counter_ = trace_data->vnf;
    }

    if (is_reduction_ && red_counter_ == 0)
      return false; // we will issue the last trace next

    // issue a cloned instruction trace
    auto trace_alloc = core_->trace_pool().allocate(1);
    auto new_trace = new (trace_alloc) instr_trace_t(*trace);
    new_trace->wb = false; // disable scoreboard release
    this->decode(new_trace);
    DT(4, "*** VOPC next lane: vl=" << vl_counter_ << ", vlmul=" << vlmul_counter_ << ", " << *new_trace);
    this->Output.push(new_trace);
    return false;
  }

  // we are done with all iterations, issue the original instruction
  this->decode(trace);
  DT(4, "*** VOPC done: " << *trace);
  this->Output.push(trace);
  return true;
}

void Ara_VOpc_Unit::decode(instr_trace_t* trace) {
  // translate to scalar pipeline
  switch (trace->fu_type) {
  case FUType::LSU:
    // no conversion
    break;
  case FUType::VPU: {
    // decode VPU instructions
    auto trace_data = std::dynamic_pointer_cast<VecUnit::ExeTraceData>(trace->data);
    switch (trace_data->vpu_op) {
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
  } break;
  default:
    assert(false);
  }

  this->lsu_flush(trace);
}

void Ara_VOpc_Unit::writeback(instr_trace_t* trace) {
  // only notify writeback for the currently active reduction instructions
  if (instr_pending_ && wb_counter_ > 0 && trace->PC == active_PC_) {
    --wb_counter_;
  }
}

void Ara_VOpc_Unit::lsu_flush(instr_trace_t* trace) {
  if (trace->fu_type != FUType::LSU)
    return;
  if (lsu_flush_) {
    trace->data = nullptr;
    return;
  }
  lsu_flush_ = true;
}

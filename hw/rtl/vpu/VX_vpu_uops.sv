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

`include "VX_define.vh"

//
// VPU uop expander.
//
module VX_vpu_uops import VX_vpu_pkg::*, VX_gpu_pkg::*; (
    input clk,
    input reset,

    VX_vpu_seq_csr_if.slave  vpu_seq_csr_if,
    VX_vpu_seq_opc_if.slave  vpu_seq_opc_if,

    input  ibuffer_t ibuf_in,
    output ibuffer_t ibuf_out,

    input  wire start,
    input  wire advance,
    input  wire [UOP_CTR_W-1:0] uop_idx,
    output wire [UOP_CTR_W-1:0] uop_count
);
    `UNUSED_VAR (start)
    `UNUSED_VAR (advance)
    `UNUSED_VAR (vpu_seq_opc_if.wis)

    // VLEN-derived widths used by RVV LSU expansion.
    //   elements_per_vreg(vsew) = VLEN >> (3 + vsew)         // SEW=8<<vsew bits
    //   simd_groups(vsew)       = max(1, elements_per_vreg / SIMD_WIDTH)
    // VLENB = VLEN/8. simd_groups = max(1, VLENB / (SIMD_WIDTH << vsew)).
    localparam SG_LOG2_MAX_W   = `CLOG2(VPU_GROUP_IDX_BITS + 1); // width to hold sg_log2

    // vpu_csrs are maintained by VX_csr_unit (VSET handler) and broadcast
    // per-warp via vpu_seq_csr_if. vpu_seq_opc_if forwards the VSET state
    // earlier so uop expansion does not run ahead with stale CSR values.
    vpu_csrs_t vpu_csrs;
    assign vpu_csrs = vpu_seq_csr_if.data;
    `UNUSED_VAR (vpu_csrs)

    vpu_states_t opc_vstate;
    always @(posedge clk) begin
        if (reset) begin
            opc_vstate <= '0;
        end else if (vpu_seq_opc_if.valid) begin
            opc_vstate <= vpu_seq_opc_if.data;
        end
    end

    wire [2:0] csr_vlmul = vpu_seq_opc_if.valid ? vpu_seq_opc_if.data.vtype.vlmul : opc_vstate.vtype.vlmul;
    wire [2:0] csr_vsew  = vpu_seq_opc_if.valid ? vpu_seq_opc_if.data.vtype.vsew  : opc_vstate.vtype.vsew;
    wire [VL_MAX_W-1:0] csr_vl = vpu_seq_opc_if.valid ? vpu_seq_opc_if.data.vl : opc_vstate.vl;

    wire csr_has_lmul = (csr_vlmul != 0);

    // RVV LSU expansion: uop_count = nfields * emul * simd_groups.
    // Whole-register form (umop==01000): nfields=1, emul=nf+1.
    // Otherwise: nfields=nf+1, emul derived from vtype.vlmul (LMUL>=1 only here;
    // fractional LMUL collapses to emul=1).
    wire is_vset    = (ibuf_in.ex_type == EX_SFU) && (ibuf_in.op_type == INST_OP_BITS'(INST_SFU_VSET));
    wire is_rvv_lsu = (ibuf_in.ex_type == EX_LSU) && ibuf_in.is_rvv;
    wire [REG_TYPE_BITS-1:0] rd_type  = get_reg_type(ibuf_in.rd);
    wire [REG_TYPE_BITS-1:0] rs1_type = get_reg_type(ibuf_in.rs1);
    wire [REG_TYPE_BITS-1:0] rs2_type = get_reg_type(ibuf_in.rs2);
    wire [REG_TYPE_BITS-1:0] rs3_type = get_reg_type(ibuf_in.rs3);
    wire is_vfmv_f_s = ibuf_in.is_rvv
                    && (ibuf_in.ex_type == EX_FPU)
                    && (ibuf_in.op_type == INST_OP_BITS'(INST_FPU_MISC))
                    && (ibuf_in.op_args.fpu.frm == 3'd5)
                    && (rd_type == REG_TYPE_F)
                    && (rs1_type == REG_TYPE_V);
    wire is_vfmv_v_f = ibuf_in.is_rvv
                    && (ibuf_in.ex_type == EX_FPU)
                    && (ibuf_in.op_type == INST_OP_BITS'(INST_FPU_MISC))
                    && (ibuf_in.op_args.fpu.frm == 3'd5)
                    && (rd_type == REG_TYPE_V)
                    && (rs1_type == REG_TYPE_F);
    wire [4:0] rvv_umop = ibuf_in.op_args.lsu.offset[11:7];
    wire is_whole_reg  = (rvv_umop == 5'b01000);
    wire is_mask_ls    = (rvv_umop == 5'b01011);
    wire [VPU_NF_BITS-1:0] nf_v = ibuf_in.op_args.lsu.nf;

    // emul from vlmul: 000->1, 001->2, 010->4, 011->8, frac (1xx)->1
    localparam EMUL_W = VPU_EMUL_IDX_BITS + 1; // wide enough to hold value 8
    reg [EMUL_W-1:0] emul_v;
    always @(*) begin
        case (csr_vlmul)
            3'b000:  emul_v = EMUL_W'(1);
            3'b001:  emul_v = EMUL_W'(2);
            3'b010:  emul_v = EMUL_W'(4);
            3'b011:  emul_v = EMUL_W'(8);
            default: emul_v = EMUL_W'(1);
        endcase
    end

    wire [EMUL_W-1:0] nfields     = EMUL_W'(nf_v) + EMUL_W'(1);
    wire [EMUL_W-1:0] eff_nfields = (is_whole_reg || is_mask_ls) ? EMUL_W'(1) : nfields;
    wire [EMUL_W-1:0] eff_emul    = is_whole_reg ? nfields : (is_mask_ls ? EMUL_W'(1) : emul_v);

    // Compute simd_groups based on current SEW.
    // simd_groups = max(1, VLENB >> (vsew + SIMD_W_LOG2))
    // Equivalently elements_per_vreg = VLENB >> vsew, then >> SIMD_W_LOG2.
    localparam SG_W = VPU_GROUP_IDX_BITS + 1; // wide enough for VPU_MAX_SIMD_GROUPS
    wire [2:0] vsew = csr_vsew;
    // For whole-register L/S, element width comes from the instruction's `width`
    // field (op_args.lsu.offset[4:2]) rather than vtype.vsew, per RVV spec.
    wire [2:0] lsu_width = ibuf_in.op_args.lsu.offset[4:2];
    wire [2:0] eff_sew   = (is_rvv_lsu && is_whole_reg) ? lsu_width : vsew;
    wire [SG_W-1:0] sg_raw = SG_W'(VPU_MAX_SIMD_GROUPS) >> eff_sew;
    wire [SG_W-1:0] sg_generic = (sg_raw == SG_W'(0)) ? SG_W'(1) : sg_raw;

    // vlm.v/vsm.v (umop==01011): uop count derived from vl bytes, not vsew/vlmul.
    // vl_bytes = ceil(vl/8). simd_groups_mask = ceil_pow2(ceil(vl_bytes/SIMD_WIDTH)).
    localparam VLB_W = VL_MAX_W + 1; // room for vl+7
    wire [VLB_W-1:0] vl_ext    = VLB_W'(csr_vl);
    wire [VLB_W-1:0] vl_bytes  = (vl_ext + VLB_W'(7)) >> 3;
    // ceil(vl_bytes / SIMD_WIDTH) via shift; since SIMD_WIDTH is pow2.
    localparam SIMD_W_LOG2 = `CLOG2(`SIMD_WIDTH);
    wire [VLB_W-1:0] groups_raw = (vl_bytes + VLB_W'(`SIMD_WIDTH - 1)) >> SIMD_W_LOG2;
    // Round up to next power of two so sg_log2 works.
    // Find highest bit position of (groups_raw - 1); result = 1 << (hb+1), else 1.
    wire [VLB_W-1:0] gr_m1 = groups_raw - VLB_W'(1);
    reg [SG_W-1:0] mask_simd_groups;
    always @(*) begin
        mask_simd_groups = SG_W'(1);
        if (groups_raw > VLB_W'(1)) begin
            for (int k = 0; k < VLB_W; k++) begin
                if (gr_m1[k]) mask_simd_groups = SG_W'(1) << (k + 1);
            end
        end
    end
    wire [SG_W-1:0] simd_groups = is_mask_ls ? mask_simd_groups : sg_generic;

    wire [VLB_W-1:0] vl_elem_groups = (vl_ext + VLB_W'(`SIMD_WIDTH - 1)) >> SIMD_W_LOG2;
    wire [UOP_CTR_W-1:0] max_elem_groups = UOP_CTR_W'(eff_emul) * UOP_CTR_W'(simd_groups);
    wire [UOP_CTR_W-1:0] active_elem_groups =
        (vl_elem_groups > VLB_W'(max_elem_groups)) ? max_elem_groups : UOP_CTR_W'(vl_elem_groups);

    wire [UOP_CTR_W-1:0] lsu_full_uop_count =
        UOP_CTR_W'(eff_nfields) * UOP_CTR_W'(eff_emul) * UOP_CTR_W'(simd_groups);
    wire [UOP_CTR_W-1:0] lsu_vl_uop_count =
        UOP_CTR_W'(eff_nfields) * active_elem_groups;
    wire [UOP_CTR_W-1:0] lsu_uop_count =
        (is_whole_reg || is_mask_ls) ? lsu_full_uop_count : lsu_vl_uop_count;

    assign uop_count = is_vset || is_vfmv_f_s || is_vfmv_v_f ? UOP_CTR_W'(1)
                     : is_rvv_lsu ? lsu_uop_count
                     : (csr_has_lmul ? UOP_CTR_W'(8) : UOP_CTR_W'(1));

    wire [2:0] ctr = uop_idx[2:0];

    // Decompose linear uop_idx into (field_idx, emul_idx, group_idx) using
    // shift-based division (all denominators are powers of 2). Layout:
    //   uop_idx = ((field_idx * emul) + emul_idx) * simd_groups + group_idx
    // Inner = group, middle = emul, outer = field.
    // sg_log2 = log2(simd_groups). Since simd_groups is a power of 2 in [1, MAX],
    // we precompute it as a small width using a priority encoder.
    reg [SG_LOG2_MAX_W-1:0] sg_log2;
    always @(*) begin
        sg_log2 = '0;
        for (int k = 0; k < VPU_GROUP_IDX_BITS + 1; k++) begin
            if (simd_groups[k]) sg_log2 = SG_LOG2_MAX_W'(k);
        end
    end

    reg [VPU_EMUL_IDX_BITS:0] em_log2; // log2(eff_emul), value in [0..3]
    always @(*) begin
        em_log2 = '0;
        for (int k = 0; k < EMUL_W; k++) begin
            if (eff_emul[k]) em_log2 = (VPU_EMUL_IDX_BITS+1)'(k);
        end
    end

    wire [UOP_CTR_W-1:0] uop_idx_w = uop_idx;
    wire [UOP_CTR_W-1:0] after_g = uop_idx_w >> sg_log2;
    wire [UOP_CTR_W-1:0] after_em = after_g >> em_log2;

    wire [UOP_CTR_W-1:0] group_idx_full = uop_idx_w & ((UOP_CTR_W'(1) << sg_log2) - UOP_CTR_W'(1));
    wire [UOP_CTR_W-1:0] emul_idx_full  = after_g & ((UOP_CTR_W'(1) << em_log2) - UOP_CTR_W'(1));
    wire [UOP_CTR_W-1:0] field_idx_full = after_em;

    wire [VPU_GROUP_IDX_BITS-1:0] group_idx_w = group_idx_full[VPU_GROUP_IDX_BITS-1:0];
    wire [VPU_EMUL_IDX_BITS-1:0]  emul_idx_w  = emul_idx_full[VPU_EMUL_IDX_BITS-1:0];
    wire [VPU_FIELD_IDX_BITS-1:0] field_idx_w = field_idx_full[VPU_FIELD_IDX_BITS-1:0];

    `UNUSED_VAR (group_idx_full)
    `UNUSED_VAR (emul_idx_full)
    `UNUSED_VAR (field_idx_full)

    // VGPR offset: vd + field_idx * emul + emul_idx (matches SimX vd + f*emul)
    wire [4:0] vgpr_off = 5'(field_idx_w) * 5'(eff_emul) + 5'(emul_idx_w);
    wire [4:0] scalar_off = 5'(ctr);
    wire [4:0] vreg_off = is_rvv_lsu ? vgpr_off : scalar_off;

    // For RVV indexed loads/stores (mop=01/11), the index vector vs2 is read once
    // per element regardless of nf/emul iteration. SimX reads getVregData(eew, vs2, i)
    // where i is the element index — so vs2 should NOT be bumped by field_idx or emul_idx.
    // It still advances with group_idx (each uop group covers a different element span).
    wire is_rvv_indexed = is_rvv_lsu && ibuf_in.op_args.lsu.offset[5];
    wire [4:0] rs2_vreg_off = is_rvv_indexed ? 5'(0) : vreg_off;

    ibuffer_t ibuf_r;
    always @(*) begin
        ibuf_r = ibuf_in;
        if (rd_type == REG_TYPE_V) begin
            ibuf_r.rd[4:0] = ibuf_in.rd[4:0] + vreg_off;
        end
        if (rs1_type == REG_TYPE_V) begin
            ibuf_r.rs1[4:0] = ibuf_in.rs1[4:0] + vreg_off;
        end
        if (rs2_type == REG_TYPE_V) begin
            ibuf_r.rs2[4:0] = ibuf_in.rs2[4:0] + rs2_vreg_off;
        end
        if (rs3_type == REG_TYPE_V) begin
            ibuf_r.rs3[4:0] = ibuf_in.rs3[4:0] + vreg_off;
        end
        if (is_rvv_lsu) begin
            ibuf_r.op_args.lsu.field_idx = field_idx_w;
            ibuf_r.op_args.lsu.emul_idx  = emul_idx_w;
            ibuf_r.op_args.lsu.group_idx = group_idx_w;
        end
        ibuf_r.wb = ibuf_in.wb;
    end

    assign ibuf_out = ibuf_r;

    if (UOP_CTR_W > 3) begin : g_unused_upper
        `UNUSED_VAR (uop_idx[UOP_CTR_W-1:3])
    end

endmodule

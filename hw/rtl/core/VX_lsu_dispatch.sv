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

// LSU dispatch slice: computes effective per-lane base address for
// pack-loads and RVV unit/strided/indexed segment loads/stores, applies
// v0 mask predication to tmask, and drives the LSU dispatch elastic
// buffer. Extracted from VX_dispatcher.sv to isolate the RVV address
// compute (the critical-path block targeted by the P0 timing fix).
module VX_lsu_dispatch import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = ""
) (
    input  wire             clk,
    input  wire             reset,

    input  wire             valid_in,
    input  operands_t       operand_data,
    output wire             ready_in,

    VX_dispatch_if.master   dispatch_if_lsu
);
    `UNUSED_SPARAM (INSTANCE_ID)

    localparam OUT_DATAW = $bits(dispatch_t);
    localparam OPD_DATAW = $bits(operands_t);

    // Pipeline stage: register the full operand bundle one cycle before
    // the RVV/pack address compute. The vopc_unit's operand flop sits
    // far from the DSP in the placed netlist (~0.35 ns routing before the
    // DSP B-port), and the compute includes a 32×XLEN cascaded multiply
    // on the strided path. Flopping locally inside lsu_dispatch lets the
    // mult start from a nearby register and leaves a full cycle for
    // mult + add + lsu_buffer write, closing the 300 MHz budget at cost
    // of one extra cycle of LSU-issue latency (all LSU ops pay equally).
    operands_t opd_q;
    wire       valid_q;
    wire       ready_q;

    VX_pipe_buffer #(
        .DATAW (OPD_DATAW),
        .DEPTH (1)
    ) opd_pipe (
        .clk       (clk),
        .reset     (reset),
        .valid_in  (valid_in),
        .data_in   (operand_data),
        .ready_in  (ready_in),
        .valid_out (valid_q),
        .data_out  (opd_q),
        .ready_out (ready_q)
    );
    `UNUSED_VAR (opd_q.ex_type)

    logic [`SIMD_WIDTH-1:0][`XLEN-1:0] eff_rs1_data;
    op_args_t eff_op_args;
    logic [`SIMD_WIDTH-1:0] lsu_eff_tmask;

    // Pack-load: compute eff_rs1[lane] = rs1[lane] + rs2[lane] * uop_idx
    // uop_idx is in op_args.lsu.offset[1:0]; stride lives in rs2_data.
    // Multiply via shift-and-add on the 2-bit index — no multiplier needed.
    wire is_pack_lsu = (opd_q.op_args.lsu.pack != 2'b00);
    wire [1:0] pld_uop_idx = opd_q.op_args.lsu.offset[1:0];
`ifdef EXT_V_ENABLE
    wire is_rvv_lsu = opd_q.is_rvv;
    // EEW bytes from RVV-overloaded offset[4:2] = width: 0->1, 1->2, 2->4, 3->8
    wire [2:0] rvv_width = opd_q.op_args.lsu.offset[4:2];
    // mop[6:5]: 00=unit-stride, 10=strided, 01/11=indexed (unhandled)
    wire [1:0] rvv_mop = opd_q.op_args.lsu.offset[6:5];
    wire rvv_is_strided = (rvv_mop == 2'b10);
    wire rvv_is_indexed = (rvv_mop[0] == 1'b1);
    // SEW/EEW byte widths are always powers of 2 (1,2,4,8), so all
    // byte-size multiplies are constant-amount shifts. The shift amount
    // is the log2 encoded directly in the instruction fields:
    //   - eew: rvv_width[1:0] already encodes log2(EEW bytes)
    //   - sew: vtype.vsew already encodes log2(SEW bytes)
    wire [1:0] log2_eew = rvv_width[1:0];
    wire [SEW_TYPE_W-1:0] log2_sew = opd_q.sew.etype;
    // Segment: address contribution = (linear_elem * nfields + field_idx) * EEW
    // where linear_elem = (emul_idx * simd_groups + group_idx) * SIMD_WIDTH + lane.
    wire [3:0] rvv_nfields   = 4'(opd_q.op_args.lsu.nf) + 4'd1;
    wire [VPU_FIELD_IDX_BITS-1:0] rvv_field_idx = opd_q.op_args.lsu.field_idx;
    wire [VPU_EMUL_IDX_BITS-1:0]  rvv_emul_idx  = opd_q.op_args.lsu.emul_idx;
    wire [VPU_GROUP_IDX_BITS-1:0] rvv_group_idx = opd_q.op_args.lsu.group_idx;
    // Whole-register form (umop=01000) collapses to nfields=1, field offset 0.
    wire rvv_whole_reg = (opd_q.op_args.lsu.offset[11:7] == 5'b01000);
    wire [3:0] eff_nfields_d  = rvv_whole_reg ? 4'd1 : rvv_nfields;
    wire [VPU_FIELD_IDX_BITS-1:0] eff_field_idx_d = rvv_whole_reg
                                                        ? VPU_FIELD_IDX_BITS'(0)
                                                        : rvv_field_idx;
    // simd_groups = VPU_MAX_SIMD_GROUPS >> vsew, clamped to 1. Both
    // VPU_MAX_SIMD_GROUPS and SIMD_WIDTH are compile-time powers of 2,
    // so (emul_idx * simd_groups) reduces to a left shift by a runtime
    // log2 amount, eliminating the DSP multiplier on the emul stride.
    localparam int LOG2_MAX_SG = `CLOG2(VPU_MAX_SIMD_GROUPS);
    localparam int LOG2_SG_W   = (LOG2_MAX_SG == 0) ? 1 : `CLOG2(LOG2_MAX_SG + 1);
    wire [LOG2_SG_W-1:0] log2_simd_groups =
        (LOG2_SG_W'(LOG2_MAX_SG) > LOG2_SG_W'(log2_sew))
            ? (LOG2_SG_W'(LOG2_MAX_SG) - LOG2_SG_W'(log2_sew))
            : LOG2_SG_W'(0);
    localparam int LOG2_SIMD_W = `CLOG2(`SIMD_WIDTH);
`else
    wire is_rvv_lsu = 1'b0;
    wire rvv_is_strided = 1'b0;
    wire rvv_is_indexed = 1'b0;
    wire [1:0] log2_eew = 2'd2;
    wire [1:0] log2_sew = 2'd2;
    wire [2:0] rvv_width = 3'b010;
    `UNUSED_VAR (rvv_width)
    `UNUSED_VAR (rvv_is_indexed)
    `UNUSED_VAR (log2_sew)
    wire [3:0] eff_nfields_d = 4'd1;
    wire [VPU_FIELD_IDX_BITS-1:0] eff_field_idx_d = '0;
    wire [VPU_EMUL_IDX_BITS-1:0]  rvv_emul_idx    = '0;
    wire [VPU_GROUP_IDX_BITS-1:0] rvv_group_idx   = '0;
    localparam int LOG2_MAX_SG = 0;
    localparam int LOG2_SG_W   = 1;
    localparam int LOG2_SIMD_W = `CLOG2(`SIMD_WIDTH);
    `UNUSED_PARAM (LOG2_MAX_SG)
    wire [LOG2_SG_W-1:0] log2_simd_groups = '0;
    `UNUSED_VAR (log2_eew)
    `UNUSED_VAR (eff_nfields_d)
    `UNUSED_VAR (eff_field_idx_d)
    `UNUSED_VAR (rvv_emul_idx)
    `UNUSED_VAR (rvv_group_idx)
    `UNUSED_VAR (log2_simd_groups)
    `UNUSED_VAR (rvv_is_strided)
`endif

    for (genvar j = 0; j < `SIMD_WIDTH; ++j) begin : g_eff_rs1
        wire [`XLEN-1:0] stride_off =
            ({`XLEN{pld_uop_idx[0]}} & (opd_q.rs2_data[j] << 0))
          + ({`XLEN{pld_uop_idx[1]}} & (opd_q.rs2_data[j] << 1));
        // RVV unit-stride segment: addr = base + (linear_elem * nfields + field_idx) * EEW
        // linear_elem = (emul_idx * simd_groups + group_idx) * SIMD_WIDTH + lane.
        // All byte-size / SIMD / simd_groups multiplies collapse to shifts
        // (every factor is a compile-time or runtime-log2 power of two),
        // which removes the cascaded DSP chain that was the P0 critical path.
        // The only remaining runtime multiplies are `linear_elem * nfields`
        // (XLEN × 4-bit, fits one DSP) and `linear_elem * stride` on the
        // strided path (XLEN × XLEN); both sit on their own without cascade.
        wire [`XLEN-1:0] emul_stride_sh  = `XLEN'(rvv_emul_idx) << log2_simd_groups;
        wire [`XLEN-1:0] linear_base     = (emul_stride_sh + `XLEN'(rvv_group_idx)) << LOG2_SIMD_W;
        wire [`XLEN-1:0] linear_elem     = linear_base + `XLEN'(j);
        wire [`XLEN-1:0] rvv_elem_idx    = linear_elem * `XLEN'(eff_nfields_d)
                                         + `XLEN'(eff_field_idx_d);
        wire [`XLEN-1:0] rvv_off         = rvv_elem_idx << log2_eew;
        // Strided: addr = base + linear_elem * stride + field_idx * EEW
        // stride is the scalar from rs2 (REG_TYPE_I, broadcast to lane 0).
        wire [`XLEN-1:0] rvv_field_eew   = `XLEN'(eff_field_idx_d) << log2_eew;
        wire [`XLEN-1:0] rvv_strided_off = linear_elem * opd_q.rs2_data[0]
                                         + rvv_field_eew;
        // Indexed: addr = base + vs2[lane] (per-lane offset) + field_idx * SEW_bytes.
        // vs2 is REG_TYPE_V tagged by decoder for mop=01/11 so rs2_data[lane] is per-lane.
        // Per spec, index element width is EEW (from instruction width field); for EEW<XLEN
        // the index must be zero-extended from the low EEW bits — mask high bits here.
        reg [`XLEN-1:0] rvv_idx;
        always_comb begin
            case (rvv_width)
                3'b000:  rvv_idx = `XLEN'(opd_q.rs2_data[j][7:0]);
                3'b001:  rvv_idx = `XLEN'(opd_q.rs2_data[j][15:0]);
                3'b010:  rvv_idx = `XLEN'(opd_q.rs2_data[j][31:0]);
                3'b011:  rvv_idx = opd_q.rs2_data[j];
                default: rvv_idx = opd_q.rs2_data[j];
            endcase
        end
        wire [`XLEN-1:0] rvv_indexed_off = rvv_idx
                                         + (`XLEN'(eff_field_idx_d) << log2_sew);
        wire [`XLEN-1:0] rvv_sel_off = rvv_is_indexed ? rvv_indexed_off
                                     : (rvv_is_strided ? rvv_strided_off : rvv_off);
        assign eff_rs1_data[j] = is_rvv_lsu
            ? (opd_q.rs1_data[j] + rvv_sel_off)
            : (is_pack_lsu
                ? (opd_q.rs1_data[j] + stride_off)
                :  opd_q.rs1_data[j]);
    end

    always_comb begin
        eff_op_args = opd_q.op_args;
        if (is_pack_lsu || is_rvv_lsu) begin
            eff_op_args.lsu.offset = '0;
        end
    end

    // RVV v0 mask predication for loads/stores.
    // When is_rvv_lsu && is_masked (vm=0), each element i is active iff v0[i] == 1.
    // Stage 4: masked LOADS overload RS3 = v0 via the decoder; loads don't use
    // rs3 otherwise. The per-lane v0 read arrives in rs3_data after the vopc
    // unpacker. For masked STORES, rs3 is already vs3 store-data; overloading
    // would require a 4th VGPR read port (bump of NUM_SRC_OPDS to 4 — 15+ file
    // change). Stores therefore retain the rs2-based stub until a 4th slot is
    // added. Note: the SEW-parametric unpacker places v0 byte `lane` into
    // rs3_data[lane], so bit 0 captures one mask bit per byte of v0; exact
    // bit-packed semantics (v0 bit `start_bit+lane`) require a dedicated
    // non-unpacked mask read path (future work).
    //
    // TODO(rvv-gap-1): masked RVV STORES real v0 read. Requires bumping
    //   NUM_SRC_OPDS from 3 to 4 and threading a 4th source operand
    //   through decode → scoreboard → operands → opc/vopc/sopc → gpr_file
    //   → dispatcher → execute header. Current store path uses
    //   rs2_data[lane][0] as a stub — represents the AND-gate fan-in for
    //   synthesis area but is NOT functionally correct for arbitrary v0.
    // TODO(rvv-gap-2): strict bit-packed v0 extraction. v0 holds vl mask
    //   bits bit-packed; element i's mask lives at v0-bit-index i. Today
    //   the SEW-parametric vopc unpacker places one v0 byte per lane in
    //   rs3_data[lane], so we consume bit[0] of that byte. This matches
    //   spec only when the SEW unpacker byte-offset aligns with the
    //   element's bit offset (dense SEW=8 mask use). For SEW>8 or
    //   partially-populated groups, fix by adding a non-unpacked mask
    //   read path that returns the SIMD_WIDTH bits at
    //   `(emul_idx*simd_groups + group_idx) * SIMD_WIDTH` within v0.
    for (genvar j = 0; j < `SIMD_WIDTH; ++j) begin : g_lsu_mask
    `ifdef EXT_V_ENABLE
        // Loads: RS3 = v0 (real read port). Stores: fall back to rs2 stub.
        wire is_rvv_store   = opd_q.op_args.lsu.is_store;
        wire mask_bit_load  = opd_q.rs3_data[j][0];
        wire mask_bit_store = opd_q.rs2_data[j][0];
        wire mask_bit = is_rvv_store ? mask_bit_store : mask_bit_load;
        wire active = ~(is_rvv_lsu & opd_q.op_args.lsu.is_masked)
                    | mask_bit;
        assign lsu_eff_tmask[j] = opd_q.tmask[j] & active;
    `else
        assign lsu_eff_tmask[j] = opd_q.tmask[j];
    `endif
    end

    // Second pipeline stage: flop the computed bundle (eff_rs1_data,
    // eff_op_args, lsu_eff_tmask) after the RVV/pack compute. This lets
    // the 32×XLEN strided multiplier output latch into the DSP48 P-register
    // instead of fanning out through the base adder + lsu_buffer input mux
    // within the same cycle — splits the DSP-cascade and the post-mult
    // base-add into two separate clock periods. Cost: +1 cycle LSU issue
    // (added on top of the first opd_pipe stage).
    wire [OUT_DATAW-1:0] cmp_bundle = {
        opd_q.uuid,
        opd_q.wis,
        opd_q.sid,
        lsu_eff_tmask,
    `ifdef EXT_V_ENABLE
        opd_q.sew,
        opd_q.is_rvv,
    `endif
        opd_q.PC,
        opd_q.wb,
        opd_q.wr_xregs,
        opd_q.rd,
        opd_q.bytesel,
        opd_q.op_type,
        eff_op_args,
        eff_rs1_data,
        opd_q.rs2_data,
        opd_q.rs3_data,
        opd_q.sop,
        opd_q.eop
    };

    wire [OUT_DATAW-1:0] cmp_bundle_q;
    wire                 cmp_valid_q;
    wire                 cmp_ready_q;

    VX_pipe_buffer #(
        .DATAW (OUT_DATAW),
        .DEPTH (1)
    ) cmp_pipe (
        .clk       (clk),
        .reset     (reset),
        .valid_in  (valid_q),
        .data_in   (cmp_bundle),
        .ready_in  (ready_q),
        .valid_out (cmp_valid_q),
        .data_out  (cmp_bundle_q),
        .ready_out (cmp_ready_q)
    );

    VX_elastic_buffer #(
        .DATAW   (OUT_DATAW),
        .SIZE    (2),
        .OUT_REG (1)
    ) lsu_buffer (
        .clk        (clk),
        .reset      (reset),
        .valid_in   (cmp_valid_q),
        .ready_in   (cmp_ready_q),
        .data_in    (cmp_bundle_q),
        .data_out   (dispatch_if_lsu.data),
        .valid_out  (dispatch_if_lsu.valid),
        .ready_out  (dispatch_if_lsu.ready)
    );

endmodule

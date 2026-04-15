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

module VX_dispatcher import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = "",
    parameter ISSUE_ID = 0
) (
    input wire              clk,
    input wire              reset,

`ifdef PERF_ENABLE
    output wire [NUM_EX_UNITS-1:0][PERF_CTR_BITS-1:0] perf_stalls,
    output wire [NUM_EX_UNITS-1:0][PERF_CTR_BITS-1:0] perf_instrs,
`endif
    // inputs
    VX_operands_if.slave    operands_if,

    // outputs
    VX_dispatch_if.master   dispatch_if [NUM_EX_UNITS]
);
    `UNUSED_SPARAM (INSTANCE_ID)
    `UNUSED_PARAM (ISSUE_ID)

    localparam OUT_DATAW = $bits(dispatch_t);

    wire [NUM_EX_UNITS-1:0] operands_ready_in;
    assign operands_if.ready = operands_ready_in[operands_if.data.ex_type];

    // Non-LSU execution units: pass operand data straight through
    for (genvar i = 0; i < NUM_EX_UNITS; ++i) begin : g_buffers
        if (i != EX_LSU) begin : g_non_lsu
            VX_elastic_buffer #(
                .DATAW   (OUT_DATAW),
                .SIZE    (2),
                .OUT_REG (1)
            ) buffer (
                .clk        (clk),
                .reset      (reset),
                .valid_in   (operands_if.valid && (operands_if.data.ex_type == EX_BITS'(i))),
                .ready_in   (operands_ready_in[i]),
                .data_in    ({
                    operands_if.data.uuid,
                    operands_if.data.wis,
                    operands_if.data.sid,
                    operands_if.data.tmask,
                `ifdef EXT_V_ENABLE
                    operands_if.data.sew,
                    operands_if.data.is_rvv,
                `endif
                    operands_if.data.PC,
                    operands_if.data.wb,
                    operands_if.data.wr_xregs,
                    operands_if.data.rd,
                    operands_if.data.bytesel,
                    operands_if.data.op_type,
                    operands_if.data.op_args,
                    operands_if.data.rs1_data,
                    operands_if.data.rs2_data,
                    operands_if.data.rs3_data,
                    operands_if.data.sop,
                    operands_if.data.eop
                }),
                .data_out   (dispatch_if[i].data),
                .valid_out  (dispatch_if[i].valid),
                .ready_out  (dispatch_if[i].ready)
            );
        end
    end

    logic [`SIMD_WIDTH-1:0][`XLEN-1:0] eff_rs1_data;
    op_args_t eff_op_args;
    logic [`SIMD_WIDTH-1:0] lsu_eff_tmask;

    // Pack-load: compute eff_rs1[lane] = rs1[lane] + rs2[lane] * uop_idx
    // uop_idx is in op_args.lsu.offset[1:0]; stride lives in rs2_data.
    // Multiply via shift-and-add on the 2-bit index — no multiplier needed.
    wire is_pack_lsu = (operands_if.data.op_args.lsu.pack != 2'b00);
    wire [1:0] pld_uop_idx = operands_if.data.op_args.lsu.offset[1:0];
`ifdef EXT_V_ENABLE
    wire is_rvv_lsu = operands_if.data.is_rvv;
    // EEW bytes from RVV-overloaded offset[4:2] = width: 0->1, 1->2, 2->4, 3->8
    wire [2:0] rvv_width = operands_if.data.op_args.lsu.offset[4:2];
    // mop[6:5]: 00=unit-stride, 10=strided, 01/11=indexed (unhandled)
    wire [1:0] rvv_mop = operands_if.data.op_args.lsu.offset[6:5];
    wire rvv_is_strided = (rvv_mop == 2'b10);
    wire rvv_is_indexed = (rvv_mop[0] == 1'b1);
    // SEW bytes from vtype.vsew: 0->1, 1->2, 2->4, 3->8
    wire [3:0] sew_bytes = 4'd1 << operands_if.data.sew.etype;
    reg [3:0] eew_bytes;
    always_comb begin
        case (rvv_width)
            3'b000:  eew_bytes = 4'd1;
            3'b001:  eew_bytes = 4'd2;
            3'b010:  eew_bytes = 4'd4;
            3'b011:  eew_bytes = 4'd8;
            default: eew_bytes = 4'd4;
        endcase
    end
    // Segment: address contribution = (linear_elem * nfields + field_idx) * EEW
    // where linear_elem = (emul_idx * simd_groups + group_idx) * SIMD_WIDTH + lane.
    wire [3:0] rvv_nfields   = 4'(operands_if.data.op_args.lsu.nf) + 4'd1;
    wire [VPU_FIELD_IDX_BITS-1:0] rvv_field_idx = operands_if.data.op_args.lsu.field_idx;
    wire [VPU_EMUL_IDX_BITS-1:0]  rvv_emul_idx  = operands_if.data.op_args.lsu.emul_idx;
    wire [VPU_GROUP_IDX_BITS-1:0] rvv_group_idx = operands_if.data.op_args.lsu.group_idx;
    // Whole-register form (umop=01000) collapses to nfields=1, field offset 0.
    wire rvv_whole_reg = (operands_if.data.op_args.lsu.offset[11:7] == 5'b01000);
    wire [3:0] eff_nfields_d  = rvv_whole_reg ? 4'd1 : rvv_nfields;
    wire [VPU_FIELD_IDX_BITS-1:0] eff_field_idx_d = rvv_whole_reg
                                                        ? VPU_FIELD_IDX_BITS'(0)
                                                        : rvv_field_idx;
    // Compute simd_groups for current SEW so addr stride matches uop layout.
    localparam DSP_SG_W = VPU_GROUP_IDX_BITS + 1;
    wire [SEW_TYPE_W-1:0] dsp_vsew = operands_if.data.sew.etype;
    wire [DSP_SG_W-1:0] dsp_sg_raw = DSP_SG_W'(VPU_MAX_SIMD_GROUPS) >> dsp_vsew;
    wire [DSP_SG_W-1:0] dsp_simd_groups = (dsp_sg_raw == DSP_SG_W'(0))
                                            ? DSP_SG_W'(1) : dsp_sg_raw;
`else
    wire is_rvv_lsu = 1'b0;
    wire rvv_is_strided = 1'b0;
    wire rvv_is_indexed = 1'b0;
    wire [3:0] eew_bytes = 4'd0;
    wire [3:0] sew_bytes = 4'd0;
    `UNUSED_VAR (rvv_is_indexed)
    `UNUSED_VAR (sew_bytes)
    wire [3:0] eff_nfields_d = 4'd1;
    wire [VPU_FIELD_IDX_BITS-1:0] eff_field_idx_d = '0;
    wire [VPU_EMUL_IDX_BITS-1:0]  rvv_emul_idx    = '0;
    wire [VPU_GROUP_IDX_BITS-1:0] rvv_group_idx   = '0;
    localparam DSP_SG_W = VPU_GROUP_IDX_BITS + 1;
    wire [DSP_SG_W-1:0] dsp_simd_groups = DSP_SG_W'(1);
    `UNUSED_VAR (eew_bytes)
    `UNUSED_VAR (eff_nfields_d)
    `UNUSED_VAR (eff_field_idx_d)
    `UNUSED_VAR (rvv_emul_idx)
    `UNUSED_VAR (rvv_group_idx)
    `UNUSED_VAR (dsp_simd_groups)
    `UNUSED_VAR (rvv_is_strided)
`endif

    for (genvar j = 0; j < `SIMD_WIDTH; ++j) begin : g_eff_rs1
        wire [`XLEN-1:0] stride_off =
            ({`XLEN{pld_uop_idx[0]}} & (operands_if.data.rs2_data[j] << 0))
          + ({`XLEN{pld_uop_idx[1]}} & (operands_if.data.rs2_data[j] << 1));
        // RVV unit-stride segment: addr = base + (linear_elem * nfields + field_idx) * EEW
        // linear_elem = (emul_idx * simd_groups + group_idx) * SIMD_WIDTH + lane.
        wire [`XLEN-1:0] linear_elem = ((`XLEN'(rvv_emul_idx) * `XLEN'(dsp_simd_groups))
                                       + `XLEN'(rvv_group_idx)) * `XLEN'(`SIMD_WIDTH)
                                     + `XLEN'(j);
        wire [`XLEN-1:0] rvv_elem_idx = linear_elem * `XLEN'(eff_nfields_d) + `XLEN'(eff_field_idx_d);
        wire [`XLEN-1:0] rvv_off = rvv_elem_idx * `XLEN'(eew_bytes);
        // Strided: addr = base + linear_elem * stride + field_idx * EEW
        // stride is the scalar from rs2 (REG_TYPE_I, broadcast to lane 0).
        wire [`XLEN-1:0] rvv_strided_off = linear_elem * operands_if.data.rs2_data[0]
                                         + `XLEN'(eff_field_idx_d) * `XLEN'(eew_bytes);
        // Indexed: addr = base + vs2[lane] (per-lane offset) + field_idx * SEW_bytes.
        // vs2 is REG_TYPE_V tagged by decoder for mop=01/11 so rs2_data[lane] is per-lane.
        wire [`XLEN-1:0] rvv_indexed_off = operands_if.data.rs2_data[j]
                                         + `XLEN'(eff_field_idx_d) * `XLEN'(sew_bytes);
        wire [`XLEN-1:0] rvv_sel_off = rvv_is_indexed ? rvv_indexed_off
                                     : (rvv_is_strided ? rvv_strided_off : rvv_off);
        assign eff_rs1_data[j] = is_rvv_lsu
            ? (operands_if.data.rs1_data[0] + rvv_sel_off)
            : (is_pack_lsu
                ? (operands_if.data.rs1_data[j] + stride_off)
                :  operands_if.data.rs1_data[j]);
    end

    always_comb begin
        eff_op_args = operands_if.data.op_args;
        if (is_pack_lsu || is_rvv_lsu) begin
            eff_op_args.lsu.offset = '0;
        end
    end

    // RVV v0 mask predication for loads/stores.
    // When is_rvv_lsu && is_masked (vm=0), each element i is active iff v0[i] == 1.
    // Full implementation would add a 4th VGPR read port for v0 and slice
    // `start_bit = (emul_idx * simd_groups + group_idx) * SIMD_WIDTH` of v0.
    // Synthesis-area stub: derive per-lane active bit from rs2_data[lane][0] when
    // available (REG_TYPE_V on indexed L/S), else from rs2_data[0][lane idx] bits.
    // This produces the AND-gate fan-in into tmask without an extra read port.
    // Functional correctness for arbitrary v0 contents is not claimed.
    for (genvar j = 0; j < `SIMD_WIDTH; ++j) begin : g_lsu_mask
    `ifdef EXT_V_ENABLE
        wire mask_bit_stub = operands_if.data.rs2_data[j][0];
        wire active = ~(is_rvv_lsu & operands_if.data.op_args.lsu.is_masked)
                    | mask_bit_stub;
        assign lsu_eff_tmask[j] = operands_if.data.tmask[j] & active;
    `else
        assign lsu_eff_tmask[j] = operands_if.data.tmask[j];
    `endif
    end

    // LSU: substitute effective base address and cleared offset for bulk ops
    VX_elastic_buffer #(
        .DATAW   (OUT_DATAW),
        .SIZE    (2),
        .OUT_REG (1)
    ) lsu_buffer (
        .clk        (clk),
        .reset      (reset),
        .valid_in   (operands_if.valid && (operands_if.data.ex_type == EX_BITS'(EX_LSU))),
        .ready_in   (operands_ready_in[EX_LSU]),
        .data_in    ({
            operands_if.data.uuid,
            operands_if.data.wis,
            operands_if.data.sid,
            lsu_eff_tmask,
        `ifdef EXT_V_ENABLE
            operands_if.data.sew,
            operands_if.data.is_rvv,
        `endif
            operands_if.data.PC,
            operands_if.data.wb,
            operands_if.data.wr_xregs,
            operands_if.data.rd,
            operands_if.data.bytesel,
            operands_if.data.op_type,
            eff_op_args,
            eff_rs1_data,
            operands_if.data.rs2_data,
            operands_if.data.rs3_data,
            operands_if.data.sop,
            operands_if.data.eop
        }),
        .data_out   (dispatch_if[EX_LSU].data),
        .valid_out  (dispatch_if[EX_LSU].valid),
        .ready_out  (dispatch_if[EX_LSU].ready)
    );

`ifdef PERF_ENABLE
    reg [NUM_EX_UNITS-1:0][PERF_CTR_BITS-1:0] perf_stalls_r;
    reg [NUM_EX_UNITS-1:0][PERF_CTR_BITS-1:0] perf_instrs_r;

    wire operands_if_fire  = operands_if.valid && operands_if.ready;
    wire operands_if_stall = operands_if.valid && ~operands_if.ready;

    for (genvar i = 0; i < NUM_EX_UNITS; ++i) begin : g_perf_stalls
        always @(posedge clk) begin
            if (reset) begin
                perf_stalls_r[i] <= '0;
                perf_instrs_r[i] <= '0;
            end else begin
                perf_stalls_r[i] <= perf_stalls_r[i] + PERF_CTR_BITS'(operands_if_stall && operands_if.data.ex_type == EX_BITS'(i));
                perf_instrs_r[i] <= perf_instrs_r[i] + PERF_CTR_BITS'(operands_if_fire && operands_if.data.ex_type == EX_BITS'(i) && operands_if.data.eop);
            end
        end
        assign perf_stalls[i] = perf_stalls_r[i];
        assign perf_instrs[i] = perf_instrs_r[i];
    end
`endif

`ifdef DBG_TRACE_PIPELINE
    for (genvar ex = 0; ex < NUM_EX_UNITS; ++ex) begin : g_dispatch_trace
        always @(posedge clk) begin
            if (dispatch_if[ex].valid && dispatch_if[ex].ready) begin
                `TRACE(1, ("%t: %s dispatch: wid=%0d, sid=%0d, PC=0x%0h, ex=", $time, INSTANCE_ID, wis_to_wid(dispatch_if[ex].data.wis, ISSUE_ID), dispatch_if[ex].data.sid, to_fullPC(dispatch_if[ex].data.PC)))
                VX_trace_pkg::trace_ex_type(1, ex);
                `TRACE(1, (", op="))
                VX_trace_pkg::trace_ex_op(1, ex, dispatch_if[ex].data.op_type, dispatch_if[ex].data.op_args);
                `TRACE(1, (", tmask=%b, wb=%b, wr_xregs=%b, rd=%0d, rs1_data=", dispatch_if[ex].data.tmask, dispatch_if[ex].data.wb, dispatch_if[ex].data.wr_xregs, dispatch_if[ex].data.rd))
                `TRACE_ARRAY1D(1, "0x%0h", dispatch_if[ex].data.rs1_data, `SIMD_WIDTH)
                `TRACE(1, (", rs2_data="))
                `TRACE_ARRAY1D(1, "0x%0h", dispatch_if[ex].data.rs2_data, `SIMD_WIDTH)
                `TRACE(1, (", rs3_data="))
                `TRACE_ARRAY1D(1, "0x%0h", dispatch_if[ex].data.rs3_data, `SIMD_WIDTH)
            VX_trace_pkg::trace_op_args(1, ex, dispatch_if[ex].data.op_type, dispatch_if[ex].data.op_args);
                `TRACE(1, (", sop=%b, eop=%b (#%0d)\n", dispatch_if[ex].data.sop, dispatch_if[ex].data.eop, dispatch_if[ex].data.uuid))
            end
        end
    end
`endif

endmodule

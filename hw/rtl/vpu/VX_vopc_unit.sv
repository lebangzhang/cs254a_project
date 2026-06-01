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

module VX_vopc_unit import VX_gpu_pkg::*, VX_vpu_pkg::*
`ifdef EXT_TCU_ENABLE
    , VX_tcu_pkg::*
`endif
; #(
    parameter `STRING INSTANCE_ID = "",
    parameter OUT_BUF = 3
) (
    input wire              clk,
    input wire              reset,

`ifdef PERF_ENABLE
    output wire [PERF_CTR_BITS-1:0] perf_stalls,
`endif

`ifdef EXT_V_ENABLE
    VX_vpu_seq_opc_if.master vpu_seq_opc_if,
`endif

    VX_writeback_if.slave   writeback_if,
    VX_operands_if.slave    soperands_if,
    input wire [NUM_SRC_OPDS-1:0][NUM_REGS_BITS-1:0] src_regs_i,
    input wire [NUM_SRC_OPDS-1:0] used_rs_i,
    VX_operands_if.master   voperands_if
);
    `UNUSED_SPARAM (INSTANCE_ID)

    localparam VGPR_FILE_SIZE  = PER_OPC_WARPS * NUM_VREGS * `NUM_THREADS * VLENB;
    localparam VGPR_DATA_SIZE  = VT_COUNT * VLENB;
    localparam VGPR_DATA_WIDTH = VGPR_DATA_SIZE * 8;
    localparam VGPR_ENTRIES    = VGPR_FILE_SIZE / VGPR_DATA_SIZE;
    localparam VGPR_ADDR_BITS  = `CLOG2(VGPR_ENTRIES / NUM_VREGS);
    localparam VGPR_ADDR_WIDTH = `UP(VGPR_ADDR_BITS);
    localparam OUT_DATAW       = $bits(operands_t);

    localparam STATE_IDLE      = 0;
    localparam STATE_FETCH     = 1;
    localparam STATE_DISPATCH  = 2;
    localparam STATE_WIDTH     = 2;
`ifdef EXT_TCU_ENABLE
    localparam WMMA_VV_TILE_M = TCU_TC_M * TCU_M_STEPS;
    localparam WMMA_VV_TILE_K = TCU_TC_K * TCU_K_STEPS;
    localparam WMMA_VV_SNAP_ROWS = (WMMA_VV_TILE_M > WMMA_VV_TILE_K) ? WMMA_VV_TILE_M : WMMA_VV_TILE_K;
    localparam WMMA_VV_SNAP_W = `UP($clog2(WMMA_VV_SNAP_ROWS));
`endif

    `UNUSED_VAR (writeback_if.data.sop)

    logic [SEW_TYPE_W-1:0] csr_sew_type;
    vpu_states_t csr_vstate;

    logic [VSIMD_IDX_W-1:0] simd_ctr, simd_ctr_n;
    logic [SEW_IDX_W-1:0] sew_ctr, sew_ctr_n;

    wire gpr_req_valid, gpr_req_ready, gpr_rsp_valid;
    wire [NUM_SRC_OPDS-1:0] gpr_req_inused;
    wire [NUM_SRC_OPDS-1:0][NUM_VREGS_BITS-1:0] gpr_req_rid;
    wire [VGPR_ADDR_WIDTH-1:0] gpr_req_addr;
    wire [NUM_SRC_OPDS-1:0][VT_COUNT-1:0][VL_COUNT-1:0][`XLEN-1:0] gpr_rsp_data;

    wire gpr_wb_valid;
    wire [NUM_VREGS_BITS-1:0]  gpr_wb_rid;
    wire [VGPR_ADDR_WIDTH-1:0] gpr_wb_addr;
    wire [VT_COUNT-1:0][VL_COUNT-1:0][`XLEN-1:0] gpr_wb_data;
    wire [VT_COUNT-1:0][VL_COUNT-1:0][XLENB-1:0] gpr_wb_byteen;

    wire [VT_COUNT-1:0][VL_COUNT-1:0][`XLEN-1:0] vmask;

`ifdef EXT_TCU_ENABLE
    wire dispatch_is_wmma_vv = (dispatch_ex_type == EX_TCU)
                            && (dispatch_op_type == INST_OP_BITS'(INST_TCU_WMMA_VV));

    wire [PER_OPC_NW_W-1:0] dispatch_opc_wid = dispatch_wis[ISSUE_WIS_W-1 -: PER_OPC_NW_W];
    wire [PER_OPC_NW_W-1:0] soperands_opc_wid = soperands_if.data.wis[ISSUE_WIS_W-1 -: PER_OPC_NW_W];

    function automatic [`XLEN-1:0] wmma_vv_format_ab(input [`XLEN-1:0] data);
        wmma_vv_format_ab = `XLEN'({data[31], data[30:23], data[22:13]})
                           | `XLEN'({19'b0, (data[12:0] & 13'b0)});
    endfunction

    function automatic logic wmma_vv_is_last_uop(input op_args_t op_args);
        wmma_vv_is_last_uop = (op_args.tcu.step_m == 4'(TCU_M_STEPS - 1))
                           && (op_args.tcu.step_n == 4'(TCU_N_STEPS - 1))
                           && (op_args.tcu.step_k == 4'(TCU_K_STEPS - 1))
                           && (op_args.tcu.fmt_d  == 4'(TCU_TC_M - 1));
    endfunction
`else
    wire dispatch_is_wmma_vv = 1'b0;
`endif

    for (genvar i = 0; i < NUM_SRC_OPDS; ++i) begin : g_src_valid
    `ifdef EXT_TCU_ENABLE
        if (i == 0) begin : g_wmma_a
            assign gpr_req_rid[i] = dispatch_is_wmma_vv
                                  ? ((wmma_vv_snap_fetch && ~wmma_vv_snap_is_b)
                                      ? (to_vreg_number(dispatch_src_regs[0]) + NUM_VREGS_BITS'(wmma_vv_snap_idx))
                                      : to_vreg_number(dispatch_src_regs[0]))
                                  : to_vreg_number(dispatch_src_regs[i]);
            assign gpr_req_inused[i] = dispatch_is_wmma_vv
                                     ? (wmma_vv_snap_fetch && ~wmma_vv_snap_is_b && (int'(wmma_vv_snap_idx) < WMMA_VV_TILE_M))
                                     : (dispatch_used_rs[i] && (get_reg_type(dispatch_src_regs[i]) == REG_TYPE_V));
        end else if (i == 1) begin : g_wmma_b
            assign gpr_req_rid[i] = dispatch_is_wmma_vv
                                  ? ((wmma_vv_snap_fetch && wmma_vv_snap_is_b)
                                      ? (to_vreg_number(dispatch_src_regs[1]) + NUM_VREGS_BITS'(wmma_vv_snap_idx))
                                      : to_vreg_number(dispatch_src_regs[1]))
                                  : to_vreg_number(dispatch_src_regs[i]);
            assign gpr_req_inused[i] = dispatch_is_wmma_vv
                                     ? (wmma_vv_snap_fetch && wmma_vv_snap_is_b && (int'(wmma_vv_snap_idx) < WMMA_VV_TILE_K))
                                     : (dispatch_used_rs[i] && (get_reg_type(dispatch_src_regs[i]) == REG_TYPE_V));
        end else begin : g_wmma_c
            assign gpr_req_rid[i] = dispatch_is_wmma_vv
                                  ? to_vreg_number(dispatch_src_regs[2])
                                  : to_vreg_number(dispatch_src_regs[i]);
            assign gpr_req_inused[i] = dispatch_is_wmma_vv
                                     ? (~wmma_vv_snap_fetch && (dispatch_op_args.tcu.step_k != 4'(0)))
                                     : (dispatch_used_rs[i] && (get_reg_type(dispatch_src_regs[i]) == REG_TYPE_V));
        end
    `else
        assign gpr_req_rid[i] = to_vreg_number(dispatch_src_regs[i]);
        assign gpr_req_inused[i] = dispatch_used_rs[i] && (get_reg_type(dispatch_src_regs[i]) == REG_TYPE_V);
    `endif
    end

    assign gpr_wb_rid = to_vreg_number(writeback_if.data.rd);

    if (VGPR_ADDR_BITS != 0) begin : g_gpr_addr
        `CONCAT(gpr_req_addr, dispatch_wis[ISSUE_WIS_W-1 -: PER_OPC_NW_W], simd_ctr, PER_OPC_NW_BITS, VSIMD_IDX_BITS)
        `CONCAT(gpr_wb_addr, writeback_if.data.wis[ISSUE_WIS_W-1 -: PER_OPC_NW_W], writeback_if.data.sid, PER_OPC_NW_BITS, VSIMD_IDX_BITS)
    end else begin : g_gpr_addr_0
        assign gpr_req_addr = '0;
        assign gpr_wb_addr = '0;
    end

    assign gpr_wb_valid = writeback_if.valid && (get_reg_type(writeback_if.data.rd) == REG_TYPE_V);

    wire soperands_is_vset = (soperands_if.data.ex_type == EX_SFU)
                          && (soperands_if.data.op_type == INST_OP_BITS'(INST_SFU_VSET));

    wire [11:0] vset_zimm = soperands_if.data.op_args.vset.zimm;
    wire [4:0]  vset_imm  = soperands_if.data.op_args.vset.imm;
    wire        vset_use_imm  = soperands_if.data.op_args.vset.use_imm;
    wire        vset_use_zimm = soperands_if.data.op_args.vset.use_zimm;
    wire        vset_rd_zero  = soperands_if.data.op_args.vset.rd_zero;
    wire        vset_rs1_zero = soperands_if.data.op_args.vset.rs1_zero;
    wire [`XLEN-1:0] vset_vtype_src = vset_use_zimm ? `XLEN'(vset_zimm) : soperands_if.data.rs2_data[0];
    wire [2:0] vset_vlmul = vset_vtype_src[2:0];
    wire [2:0] vset_vsew  = vset_vtype_src[5:3];
    wire       vset_vta   = vset_vtype_src[6];
    wire       vset_vma   = vset_vtype_src[7];
    wire [VL_MAX_W-1:0] vset_vlmax = vlmax_cacl(vset_vlmul, vset_vsew);
    wire vset_vill = vill_calc(vset_vsew, vset_vlmax);
    wire [VL_MAX_W-1:0] vset_rs1_avl = VL_MAX_W'(soperands_if.data.rs1_data[0]);

    reg [VL_MAX_W-1:0] vset_vl_pre_clamp;
    always @(*) begin
        if (vset_use_imm) begin
            vset_vl_pre_clamp = VL_MAX_W'({1'b0, vset_imm});
        end else if (!vset_rs1_zero) begin
            vset_vl_pre_clamp = vset_rs1_avl;
        end else if (!vset_rd_zero) begin
            vset_vl_pre_clamp = vset_vlmax;
        end else begin
            vset_vl_pre_clamp = csr_vstate.vl;
        end
    end
    wire [VL_MAX_W-1:0] vset_vl_clamped = (vset_vl_pre_clamp > vset_vlmax) ? vset_vlmax : vset_vl_pre_clamp;
    wire [VL_MAX_W-1:0] vset_new_vl = vset_vill ? VL_MAX_W'(0) : vset_vl_clamped;

    vpu_states_t vset_new_state;
    always @(*) begin
        vset_new_state.vl = vset_new_vl;
        if (vset_vill) begin
            vset_new_state.vtype.vill = 1'b1;
            vset_new_state.vtype.reserved = '0;
            vset_new_state.vtype.vma = 1'b0;
            vset_new_state.vtype.vta = 1'b0;
            vset_new_state.vtype.vsew = 3'b000;
            vset_new_state.vtype.vlmul = 3'b000;
        end else begin
            vset_new_state.vtype.vill = 1'b0;
            vset_new_state.vtype.reserved = '0;
            vset_new_state.vtype.vma = vset_vma;
            vset_new_state.vtype.vta = vset_vta;
            vset_new_state.vtype.vsew = vset_vsew;
            vset_new_state.vtype.vlmul = vset_vlmul;
        end
    end

    logic gpr_req_sent, gpr_req_sent_n;

    assign gpr_req_valid = (state == STATE_FETCH) && ~gpr_req_sent;

    wire [VT_COUNT-1:0][VL_COUNT-1:0][`XLEN-1:0] gpr_wb_data_m;
    wire [VT_COUNT-1:0][VL_COUNT-1:0][XLENB-1:0] gpr_wb_byteen_m;

    for (genvar t = 0; t < VT_COUNT; ++t) begin : g_pack
        wire [VL_COUNT-1:0][`XLEN-1:0] gpr_wb_data_um;
        wire [VL_COUNT-1:0][XLENB-1:0] gpr_wb_byteen_um;

        VX_vpu_pack #(
            .NUM_LANES (VL_COUNT)
        ) pack (
            .sew_type (writeback_if.data.sew.etype),
            .sew_idx  (writeback_if.data.sew.idx),
            .data_in  (writeback_if.data.data[VL_COUNT * t +: VL_COUNT]),
            .mask_in  (writeback_if.data.tmask[VL_COUNT * t +: VL_COUNT]),
            .data_out (gpr_wb_data_um),
            .mask_out (gpr_wb_byteen_um)
        );
        assign gpr_wb_data_m[t] = gpr_wb_data_um;
        assign gpr_wb_byteen_m[t] = gpr_wb_byteen_um;

        assign gpr_wb_data[t]  = writeback_if.data.sew.masked ? gpr_wb_data_m[t]  : gpr_wb_data_um;
        assign gpr_wb_byteen[t] = writeback_if.data.sew.masked ? gpr_wb_byteen_m[t] : gpr_wb_byteen_um;
    end

    VX_gpr_file #(
        .FILE_SIZE (VGPR_FILE_SIZE),
        .REG_COUNT (NUM_VREGS),
        .NUM_REQS  (NUM_SRC_OPDS),
        .NUM_BANKS (`NUM_VGPR_BANKS),
        .DATA_SIZE (VGPR_DATA_SIZE),
        .TAG_WIDTH (1),
        .RDW_MODE  ("W")
    ) vgpr_file (
        .clk          (clk),
        .reset        (reset),
    `ifdef PERF_ENABLE
        .perf_stalls  (perf_stalls),
    `endif
        .req_rd_valid (gpr_req_valid),
        .req_rd_inused(gpr_req_inused),
        .req_rd_rid   (gpr_req_rid),
        .req_rd_addr  (gpr_req_addr),
        .req_rd_tag   (1'b0),
        .req_rd_ready (gpr_req_ready),
        .rsp_rd_valid (gpr_rsp_valid),
        .rsp_rd_data  (gpr_rsp_data),
        `UNUSED_PIN(rsp_rd_tag),
        .rsp_rd_ready (1'b1),
        .req_wr_valid (gpr_wb_valid),
        .req_wr_addr  (gpr_wb_addr),
        .req_wr_rid   (gpr_wb_rid),
        .req_wr_mask  (gpr_wb_byteen),
        .req_wr_data  (gpr_wb_data)
    );

    // Vector Mask Register Bank
    VX_dp_ram #(
        .DATAW (VGPR_DATA_WIDTH),
        .SIZE  (PER_ISSUE_WARPS * VSIMD_COUNT),
        .WRENW (VGPR_DATA_SIZE),
        .RDW_MODE ("R"),
        .RADDR_REG (1)
    ) vmask_bank (
        .clk   (clk),
        .reset (reset),
        .read  (1'b1),
        .wren  (gpr_wb_byteen_m),
        .write (gpr_wb_valid),
        .waddr (gpr_wb_addr),
        .wdata (gpr_wb_data_m),
        .raddr (gpr_req_addr),
        .rdata (vmask)
    );

`ifdef DBG_TRACE_PIPELINE
    always @(posedge clk) begin
        if (gpr_wb_valid) begin
            `TRACE(1, ("%t: %s-vgpr-wr: PC=0x%0h, wis=%0d, sid=%0d, addr=%0d, rd=%0d, sew=%0d/%0d, tmask=%b, byteen=",
                $time, INSTANCE_ID, to_fullPC(writeback_if.data.PC), writeback_if.data.wis, writeback_if.data.sid,
                gpr_wb_addr, gpr_wb_rid, writeback_if.data.sew.etype, writeback_if.data.sew.idx, writeback_if.data.tmask))
            `TRACE_ARRAY1D(1, "0x%0h", gpr_wb_byteen[0], VL_COUNT)
            `TRACE(1, (", data="))
            `TRACE_ARRAY1D(1, "0x%0h", gpr_wb_data[0], VL_COUNT)
            `TRACE(1, (" (#%0d)\n", writeback_if.data.uuid))
        end
        if (gpr_req_valid) begin
            `TRACE(1, ("%t: %s-vgpr-rd-req: PC=0x%0h, wis=%0d, sid=%0d, addr=%0d, used=%b, rids={%0d,%0d,%0d}\n",
                $time, INSTANCE_ID, to_fullPC(dispatch_PC), dispatch_wis, simd_ctr, gpr_req_addr,
                gpr_req_inused, gpr_req_rid[2], gpr_req_rid[1], gpr_req_rid[0]))
        end
        if (gpr_rsp_valid) begin
            `TRACE(1, ("%t: %s-vgpr-rd-rsp: PC=0x%0h, wis=%0d, sid=%0d, addr=%0d, rs1=",
                $time, INSTANCE_ID, to_fullPC(dispatch_PC), dispatch_wis, simd_ctr, gpr_req_addr))
            `TRACE_ARRAY1D(1, "0x%0h", gpr_rsp_data[0][0], VL_COUNT)
            `TRACE(1, (", rs2="))
            `TRACE_ARRAY1D(1, "0x%0h", gpr_rsp_data[1][0], VL_COUNT)
            `TRACE(1, (", rs3="))
            `TRACE_ARRAY1D(1, "0x%0h", gpr_rsp_data[2][0], VL_COUNT)
            `TRACE(1, ("\n"))
        end
    end
`endif

    wire [NUM_SRC_OPDS-1:0][VT_COUNT-1:0][VL_COUNT-1:0][`XLEN-1:0] unpacked_data;
    wire [VT_COUNT-1:0][VL_COUNT-1:0] unpacked_vmask;

    wire is_signed = ~insn_is_unsigned(dispatch_ex_type, dispatch_op_type);
    wire sew_masked = insn_is_masked(dispatch_ex_type, dispatch_op_type);

    for (genvar t = 0; t < VT_COUNT; ++t) begin : g_unpack
        for (genvar i = 0; i < NUM_SRC_OPDS; ++i) begin : g_i
            VX_vpu_unpack #(
                .NUM_LANES (VL_COUNT)
            ) unpack (
                .sew_type  (csr_sew_type),
                .sew_idx   (sew_ctr),
                .is_signed (is_signed),
                .data_in   (gpr_rsp_data[i][t]),
                .data_out  (unpacked_data[i][t])
            );
        end

        VX_vpu_unpack_mask #(
            .NUM_LANES (VL_COUNT)
        ) unpack_mask (
            .sew_type  (csr_sew_type),
            .sew_idx   (sew_ctr),
            .data_in   (vmask[t]),
            .data_out  (unpacked_vmask[t])
        );
    end

    wire soperands_fire = soperands_if.valid && soperands_if.ready;

    logic dispatch_valid, dispatch_ready;
    logic [`SIMD_WIDTH-1:0] dispatch_tmask, dispatch_tmask_n;
    logic [EX_BITS-1:0] dispatch_ex_type, dispatch_ex_type_n;
    logic [INST_OP_BITS-1:0] dispatch_op_type, dispatch_op_type_n;
    op_args_t dispatch_op_args, dispatch_op_args_n;
    logic [UUID_WIDTH-1:0] dispatch_uuid, dispatch_uuid_n;
    logic [ISSUE_WIS_W-1:0] dispatch_wis, dispatch_wis_n;
    logic [PC_BITS-1:0] dispatch_PC, dispatch_PC_n;
    logic dispatch_is_masked, dispatch_is_masked_n;
    logic dispatch_wb, dispatch_wb_n;
    logic [NUM_XREGS-1:0] dispatch_wr_xregs, dispatch_wr_xregs_n;
    logic [NUM_REGS_BITS-1:0] dispatch_rd, dispatch_rd_n;
    logic [BYTESEL_BITS-1:0] dispatch_bytesel, dispatch_bytesel_n;
    logic [`NUM_THREADS-1:0] dispatch_thread_tmask, dispatch_thread_tmask_n;
    logic [VT_COUNT-1:0] dispatch_vt_tmask, dispatch_vt_tmask_n;
    logic [NUM_SRC_OPDS-1:0][NUM_REGS_BITS-1:0] dispatch_src_regs, dispatch_src_regs_n;
    logic [NUM_SRC_OPDS-1:0] dispatch_used_rs, dispatch_used_rs_n;
    logic [NUM_SRC_OPDS-1:0][`SIMD_WIDTH-1:0][`XLEN-1:0] dispatch_srs_data, dispatch_srs_data_n;
    logic [NUM_SRC_OPDS-1:0][`SIMD_WIDTH-1:0][`XLEN-1:0] dispatch_rs_data, dispatch_rs_data_n;
    logic dispatch_sop, dispatch_sop_n;
    logic dispatch_eop, dispatch_eop_n;
`ifdef EXT_TCU_ENABLE
    logic wmma_vv_snap_fetch, wmma_vv_snap_fetch_n;
    logic wmma_vv_snap_is_b, wmma_vv_snap_is_b_n;
    logic [WMMA_VV_SNAP_W-1:0] wmma_vv_snap_idx, wmma_vv_snap_idx_n;
    logic [PER_OPC_WARPS-1:0] wmma_vv_snap_valid, wmma_vv_snap_valid_n;
    logic [PER_OPC_WARPS-1:0][WMMA_VV_TILE_M-1:0][`SIMD_WIDTH-1:0][`XLEN-1:0] wmma_vv_a_rows, wmma_vv_a_rows_n;
    logic [`SIMD_WIDTH-1:0][`XLEN-1:0] wmma_vv_c_row, wmma_vv_c_row_n;
    logic [PER_OPC_WARPS-1:0][WMMA_VV_TILE_K-1:0][`SIMD_WIDTH-1:0][`XLEN-1:0] wmma_vv_b_rows, wmma_vv_b_rows_n;
`endif

    wire soperands_is_rvv_lsu = (soperands_if.data.ex_type == EX_LSU);
`ifdef EXT_TCU_ENABLE
    wire soperands_is_wmma_vv = (soperands_if.data.ex_type == EX_TCU)
                             && (soperands_if.data.op_type == INST_OP_BITS'(INST_TCU_WMMA_VV));
`else
    wire soperands_is_wmma_vv = 1'b0;
`endif
    wire soperands_is_vfmv_f_s = soperands_if.data.is_rvv
                              && (soperands_if.data.ex_type == EX_FPU)
                              && (soperands_if.data.op_type == INST_OP_BITS'(INST_FPU_MISC))
                              && (soperands_if.data.op_args.fpu.frm == 3'd5)
                              && (get_reg_type(soperands_if.data.rd) == REG_TYPE_F)
                              && (get_reg_type(src_regs_i[0]) == REG_TYPE_V);
    wire dispatch_is_rvv_lsu = (dispatch_ex_type == EX_LSU);
    wire dispatch_is_vset = (dispatch_ex_type == EX_SFU)
                         && (dispatch_op_type == INST_OP_BITS'(INST_SFU_VSET));
    wire dispatch_is_vfmv_f_s = (dispatch_ex_type == EX_FPU)
                             && (dispatch_op_type == INST_OP_BITS'(INST_FPU_MISC))
                             && (dispatch_op_args.fpu.frm == 3'd5)
                             && (get_reg_type(dispatch_rd) == REG_TYPE_F)
                             && (get_reg_type(dispatch_src_regs[0]) == REG_TYPE_V);

    wire dispatch_use_vmask = dispatch_is_masked && (dispatch_ex_type != EX_LSU);

    function automatic [`XLEN-1:0] format_voperand_data(
        input [EX_BITS-1:0]        ex_type,
        input [SEW_TYPE_W-1:0]     sew_type,
        input [`XLEN-1:0]          data
    );
    `ifdef XLEN_64
        if ((ex_type == EX_FPU) && (sew_type == SEW_TYPE_W'(2))) begin
            format_voperand_data = {32'hffffffff, data[31:0]};
        end else begin
            format_voperand_data = data;
        end
    `else
        `UNUSED_VAR ({ex_type, sew_type})
        format_voperand_data = data;
    `endif
    endfunction

    op_args_t dispatch_op_args_f;
    always @(*) begin
        dispatch_op_args_f = dispatch_op_args;
        if (dispatch_ex_type == EX_FPU) begin
            dispatch_op_args_f.fpu.fmt[0] = (csr_sew_type == SEW_TYPE_W'(3));
        end
    end

    localparam RVV_LSU_SUBIDX_BITS_W = `CLOG2(SEW_IDX_W + 1);

    wire [RVV_LSU_SUBIDX_BITS_W-1:0] rvv_subidx_bits =
        (RVV_LSU_SUBIDX_BITS_W'(SEW_IDX_W) > RVV_LSU_SUBIDX_BITS_W'(csr_sew_type))
            ? (RVV_LSU_SUBIDX_BITS_W'(SEW_IDX_W) - RVV_LSU_SUBIDX_BITS_W'(csr_sew_type))
            : RVV_LSU_SUBIDX_BITS_W'(0);

    wire [SEW_IDX_W:0] rvv_subidx_count = (SEW_IDX_W+1)'(1) << rvv_subidx_bits;
    wire [SEW_IDX_W-1:0] rvv_subidx_last = SEW_IDX_W'(rvv_subidx_count - (SEW_IDX_W+1)'(1));

    wire [SEW_IDX_W-1:0] rvv_lsu_sew_idx = SEW_IDX_W'(soperands_if.data.op_args.lsu.group_idx);
    wire [VL_MAX_W-1:0] dispatch_elem_base =
        (dispatch_is_rvv_lsu ? VL_MAX_W'(dispatch_op_args.lsu.group_idx) : VL_MAX_W'(sew_ctr)) * VL_MAX_W'(VL_COUNT);

    logic [STATE_WIDTH-1:0] state, state_n;

    always @(*) begin
        state_n = state;
        simd_ctr_n = simd_ctr;
        sew_ctr_n = sew_ctr;
        dispatch_tmask_n = dispatch_tmask;
        dispatch_ex_type_n = dispatch_ex_type;
        dispatch_op_type_n = dispatch_op_type;
        dispatch_op_args_n = dispatch_op_args;
        dispatch_uuid_n = dispatch_uuid;
        dispatch_wis_n = dispatch_wis;
        dispatch_PC_n = dispatch_PC;
        dispatch_is_masked_n = dispatch_is_masked;
        dispatch_wb_n = dispatch_wb;
        dispatch_wr_xregs_n = dispatch_wr_xregs;
        dispatch_rd_n = dispatch_rd;
        dispatch_bytesel_n = dispatch_bytesel;
        dispatch_thread_tmask_n = dispatch_thread_tmask;
        dispatch_vt_tmask_n = dispatch_vt_tmask;
        dispatch_src_regs_n = dispatch_src_regs;
        dispatch_used_rs_n = dispatch_used_rs;
        dispatch_srs_data_n = dispatch_srs_data;
        dispatch_rs_data_n = dispatch_rs_data;
        dispatch_sop_n = dispatch_sop;
        dispatch_eop_n = dispatch_eop;
        gpr_req_sent_n = gpr_req_sent;
    `ifdef EXT_TCU_ENABLE
        wmma_vv_snap_fetch_n = wmma_vv_snap_fetch;
        wmma_vv_snap_is_b_n = wmma_vv_snap_is_b;
        wmma_vv_snap_idx_n = wmma_vv_snap_idx;
        wmma_vv_snap_valid_n = wmma_vv_snap_valid;
        wmma_vv_a_rows_n = wmma_vv_a_rows;
        wmma_vv_c_row_n = wmma_vv_c_row;
        wmma_vv_b_rows_n = wmma_vv_b_rows;
    `endif

        case (state)
        STATE_IDLE: begin
            if (soperands_fire) begin
                state_n = soperands_is_vset ? STATE_DISPATCH : STATE_FETCH;
                gpr_req_sent_n = 1'b0;
            `ifdef EXT_TCU_ENABLE
                wmma_vv_c_row_n = '0;
                wmma_vv_snap_fetch_n = 1'b0;
                wmma_vv_snap_is_b_n = 1'b0;
                wmma_vv_snap_idx_n = '0;
            `endif
                simd_ctr_n = soperands_is_vset ? VSIMD_IDX_W'(0) :
                              soperands_is_rvv_lsu ? VSIMD_IDX_W'(soperands_if.data.op_args.lsu.group_idx) :
                              soperands_is_wmma_vv ? VSIMD_IDX_W'(0) :
                              VSIMD_IDX_W'(VSIMD_COUNT - 1);
                sew_ctr_n  = soperands_is_vset || soperands_is_vfmv_f_s ? SEW_IDX_W'(0) :
                             soperands_is_rvv_lsu ? rvv_lsu_sew_idx :
                             rvv_subidx_last;
                dispatch_ex_type_n = soperands_if.data.ex_type;
                dispatch_op_type_n = soperands_if.data.op_type;
                dispatch_op_args_n = soperands_if.data.op_args;
                dispatch_uuid_n    = soperands_if.data.uuid;
                dispatch_wis_n     = soperands_if.data.wis;
                dispatch_PC_n      = soperands_if.data.PC;
                dispatch_is_masked_n = soperands_if.data.is_masked;
                dispatch_wb_n      = soperands_if.data.wb;
                dispatch_wr_xregs_n= soperands_if.data.wr_xregs;
                dispatch_rd_n      = soperands_if.data.rd;
                dispatch_bytesel_n = soperands_if.data.bytesel;
                dispatch_thread_tmask_n = `NUM_THREADS'(soperands_if.data.tmask);
                dispatch_vt_tmask_n= soperands_if.data.tmask[VT_COUNT-1:0];
                dispatch_src_regs_n= src_regs_i;
                dispatch_used_rs_n = used_rs_i;
                dispatch_tmask_n   = soperands_is_vfmv_f_s ? '0 : soperands_if.data.tmask;
                dispatch_rs_data_n = {
                    soperands_if.data.rs3_data,
                    soperands_if.data.rs2_data,
                    soperands_if.data.rs1_data
                };
                dispatch_srs_data_n = dispatch_rs_data_n;
                dispatch_sop_n     = 1'b1;
                dispatch_eop_n     = soperands_is_vset || soperands_is_rvv_lsu || soperands_is_wmma_vv || soperands_is_vfmv_f_s || (VSIMD_COUNT == 1);
            `ifdef EXT_TCU_ENABLE
                if (soperands_is_wmma_vv) begin
                    if (wmma_vv_snap_valid[soperands_opc_wid]) begin
                        if (soperands_if.data.op_args.tcu.step_k == 4'(0)) begin
                            state_n = STATE_DISPATCH;
                            dispatch_tmask_n = '1;
                            dispatch_rs_data_n = '0;
                            for (int kk = 0; kk < TCU_TC_K; ++kk) begin
                                int a_elem;
                                a_elem = int'(soperands_if.data.op_args.tcu.step_k) * TCU_TC_K + kk;
                                if (a_elem < `SIMD_WIDTH) begin
                                    dispatch_rs_data_n[0][int'(soperands_if.data.op_args.tcu.fmt_d) * TCU_TC_K + kk] =
                                        wmma_vv_format_ab(wmma_vv_a_rows[soperands_opc_wid][int'(soperands_if.data.op_args.tcu.step_m) * TCU_TC_M + int'(soperands_if.data.op_args.tcu.fmt_d)][a_elem]);
                                end
                                for (int jj = 0; jj < TCU_TC_N; ++jj) begin
                                    int b_elem;
                                    b_elem = int'(soperands_if.data.op_args.tcu.step_n) * TCU_TC_N + jj;
                                    if (b_elem < `SIMD_WIDTH) begin
                                        dispatch_rs_data_n[1][jj * TCU_TC_K + kk] =
                                            wmma_vv_format_ab(wmma_vv_b_rows[soperands_opc_wid][int'(soperands_if.data.op_args.tcu.step_k) * TCU_TC_K + kk][b_elem]);
                                    end
                                end
                            end
                        end
                    end else begin
                        wmma_vv_snap_fetch_n = 1'b1;
                        wmma_vv_snap_is_b_n = 1'b0;
                        wmma_vv_snap_idx_n = '0;
                    end
                end
            `endif
            end
        end
        STATE_FETCH: begin
            if (gpr_req_valid && gpr_req_ready) begin
                gpr_req_sent_n = 1'b1;
            end
            if (gpr_req_sent && gpr_rsp_valid) begin
            `ifdef EXT_TCU_ENABLE
                if (dispatch_is_wmma_vv) begin
                    if (wmma_vv_snap_fetch) begin
                        if (~wmma_vv_snap_is_b) begin
                            if (wmma_vv_snap_idx == WMMA_VV_SNAP_W'(WMMA_VV_TILE_M - 1)) begin
                                wmma_vv_snap_is_b_n = 1'b1;
                                wmma_vv_snap_idx_n = '0;
                                gpr_req_sent_n = 1'b0;
                                state_n = STATE_FETCH;
                            end else begin
                                wmma_vv_snap_idx_n = wmma_vv_snap_idx + WMMA_VV_SNAP_W'(1);
                                gpr_req_sent_n = 1'b0;
                                state_n = STATE_FETCH;
                            end
                        end else begin
                            if (wmma_vv_snap_idx == WMMA_VV_SNAP_W'(WMMA_VV_TILE_K - 1)) begin
                                wmma_vv_snap_fetch_n = 1'b0;
                                wmma_vv_snap_is_b_n = 1'b0;
                                wmma_vv_snap_idx_n = '0;
                                wmma_vv_snap_valid_n[dispatch_opc_wid] = 1'b1;
                                if (dispatch_op_args.tcu.step_k == 4'(0)) begin
                                    state_n = STATE_DISPATCH;
                                end else begin
                                    gpr_req_sent_n = 1'b0;
                                    state_n = STATE_FETCH;
                                end
                            end else begin
                                wmma_vv_snap_idx_n = wmma_vv_snap_idx + WMMA_VV_SNAP_W'(1);
                                gpr_req_sent_n = 1'b0;
                                state_n = STATE_FETCH;
                            end
                        end
                    end else begin
                        state_n = STATE_DISPATCH;
                    end
                end else
            `endif
                if (dispatch_is_vfmv_f_s && simd_ctr != 0) begin
                    simd_ctr_n = simd_ctr - 1;
                    gpr_req_sent_n = 1'b0;
                    state_n = STATE_FETCH;
                end else begin
                    state_n = STATE_DISPATCH;
                end
            end
        `ifdef EXT_TCU_ENABLE
            if (dispatch_is_wmma_vv) begin
                if (gpr_req_sent && gpr_rsp_valid) begin
                    for (int t = 0; t < VT_COUNT; ++t) begin
                        for (int l = 0; l < VL_COUNT; ++l) begin
                            int elem_idx;
                            elem_idx = t * VL_COUNT + l;
                            if (elem_idx < `SIMD_WIDTH) begin
                                if (wmma_vv_snap_fetch) begin
                                    if (~wmma_vv_snap_is_b && (int'(wmma_vv_snap_idx) < WMMA_VV_TILE_M)) begin
                                        wmma_vv_a_rows_n[dispatch_opc_wid][int'(wmma_vv_snap_idx)][elem_idx] = gpr_rsp_data[0][t][l];
                                    end
                                    if (wmma_vv_snap_is_b && (int'(wmma_vv_snap_idx) < WMMA_VV_TILE_K)) begin
                                        wmma_vv_b_rows_n[dispatch_opc_wid][int'(wmma_vv_snap_idx)][elem_idx] = gpr_rsp_data[1][t][l];
                                    end
                                end else begin
                                    wmma_vv_c_row_n[elem_idx] = (dispatch_op_args.tcu.step_k == 4'(0))
                                                              ? '0
                                                              : gpr_rsp_data[2][t][l];
                                end
                            end
                        end
                    end

                    if (~wmma_vv_snap_fetch
                     || (wmma_vv_snap_is_b && (wmma_vv_snap_idx == WMMA_VV_SNAP_W'(WMMA_VV_TILE_K - 1)))) begin
                        dispatch_tmask_n = '1;
                        dispatch_rs_data_n = '0;
                        for (int kk = 0; kk < TCU_TC_K; ++kk) begin
                            int a_elem;
                            a_elem = int'(dispatch_op_args.tcu.step_k) * TCU_TC_K + kk;
                            if (a_elem < `SIMD_WIDTH) begin
                                dispatch_rs_data_n[0][int'(dispatch_op_args.tcu.fmt_d) * TCU_TC_K + kk] =
                                    wmma_vv_format_ab(wmma_vv_a_rows_n[dispatch_opc_wid][int'(dispatch_op_args.tcu.step_m) * TCU_TC_M + int'(dispatch_op_args.tcu.fmt_d)][a_elem]);
                            end
                            for (int jj = 0; jj < TCU_TC_N; ++jj) begin
                                int b_elem;
                                b_elem = int'(dispatch_op_args.tcu.step_n) * TCU_TC_N + jj;
                                if (b_elem < `SIMD_WIDTH) begin
                                    dispatch_rs_data_n[1][jj * TCU_TC_K + kk] =
                                        wmma_vv_format_ab(wmma_vv_b_rows_n[dispatch_opc_wid][int'(dispatch_op_args.tcu.step_k) * TCU_TC_K + kk][b_elem]);
                                end
                            end
                        end
                        for (int jj = 0; jj < TCU_TC_N; ++jj) begin
                            int c_elem;
                            c_elem = int'(dispatch_op_args.tcu.step_n) * TCU_TC_N + jj;
                            if (c_elem < `SIMD_WIDTH) begin
                                dispatch_rs_data_n[2][int'(dispatch_op_args.tcu.fmt_d) * TCU_TC_N + jj] = wmma_vv_c_row_n[c_elem];
                            end
                        end
                    end
                end
            end else
        `endif
            if (dispatch_is_vfmv_f_s) begin
                if (gpr_req_sent && gpr_rsp_valid) begin
                    for (int t = 0; t < VT_COUNT; ++t) begin
                        int thread_idx;
                        thread_idx = int'(simd_ctr) * VT_COUNT + t;
                        if ((thread_idx < `NUM_THREADS) && (thread_idx < `SIMD_WIDTH)) begin
                            dispatch_tmask_n[thread_idx] = dispatch_thread_tmask[thread_idx];
                            dispatch_rs_data_n[0][thread_idx] = format_voperand_data(
                                dispatch_ex_type,
                                csr_sew_type,
                                unpacked_data[0][t][0]
                            );
                        end
                    end
                end
            end else begin
                // convert to scalar micro-op
                for (int t = 0; t < VT_COUNT; ++t) begin
                    for (int l = 0; l < VL_COUNT; ++l) begin
                        int thread_idx;
                        thread_idx = int'(simd_ctr) * VT_COUNT + t;
                        dispatch_tmask_n[t * VL_COUNT + l] = (thread_idx < `NUM_THREADS)
                                                           && dispatch_thread_tmask[0]
                                                           && ((dispatch_elem_base + VL_MAX_W'(l)) < csr_vstate.vl)
                                                           && (~dispatch_use_vmask || unpacked_vmask[t][l]);
                    end
                end
                for (int i = 0; i < NUM_SRC_OPDS; ++i) begin
                    if (get_reg_type(dispatch_src_regs[i]) == REG_TYPE_V) begin
                        for (int t = 0; t < VT_COUNT; ++t) begin
                            for (int l = 0; l < VL_COUNT; ++l) begin
                                dispatch_rs_data_n[i][t * VL_COUNT + l] = format_voperand_data(
                                    dispatch_ex_type,
                                    csr_sew_type,
                                    unpacked_data[i][t][l]
                                );
                            end
                        end
                    end else if (dispatch_used_rs[i]) begin
                        for (int t = 0; t < VT_COUNT; ++t) begin
                            for (int l = 0; l < VL_COUNT; ++l) begin
                                int thread_idx;
                                thread_idx = int'(simd_ctr) * VT_COUNT + t;
                                if (thread_idx < `SIMD_WIDTH) begin
                                    dispatch_rs_data_n[i][t * VL_COUNT + l] = dispatch_srs_data[i][thread_idx];
                                end else begin
                                    dispatch_rs_data_n[i][t * VL_COUNT + l] = '0;
                                end
                            end
                        end
                    end
                end
            end
        end
        STATE_DISPATCH: begin
            if (dispatch_ready) begin
            `ifdef EXT_TCU_ENABLE
                if (dispatch_is_wmma_vv && wmma_vv_is_last_uop(dispatch_op_args)) begin
                    wmma_vv_snap_valid_n[dispatch_opc_wid] = 1'b0;
                end
            `endif
                if (dispatch_is_vset
             `ifdef EXT_TCU_ENABLE
                 || dispatch_is_wmma_vv
             `endif
                 || dispatch_is_rvv_lsu
                 || (sew_ctr == 0 && simd_ctr == 0)) begin
                    state_n = STATE_IDLE;
                end else begin
                    if (sew_ctr != 0) begin
                        sew_ctr_n = sew_ctr - 1;
                    end else begin
                        sew_ctr_n = rvv_subidx_last;
                        if (simd_ctr != 0) begin
                            simd_ctr_n = simd_ctr - 1;
                        end
                    end
                    dispatch_sop_n = 1'b0;
                    dispatch_eop_n = dispatch_is_rvv_lsu ? (simd_ctr_n == 0)
                                                         : ((simd_ctr_n == 0) && (sew_ctr_n == 0));
                    state_n = STATE_FETCH;
                    gpr_req_sent_n = 1'b0;
                end
            end
        end
        endcase
    end

    always @(posedge clk) begin
        if (reset) begin
            state <= STATE_IDLE;
            simd_ctr <= 0;
            sew_ctr <= 0;
            gpr_req_sent <= 0;
        `ifdef EXT_TCU_ENABLE
            wmma_vv_snap_fetch <= 1'b0;
            wmma_vv_snap_is_b <= 1'b0;
            wmma_vv_snap_idx <= '0;
            wmma_vv_snap_valid <= '0;
            wmma_vv_a_rows <= '0;
            wmma_vv_c_row <= '0;
            wmma_vv_b_rows <= '0;
        `endif
        end else begin
            state <= state_n;
            dispatch_tmask <= dispatch_tmask_n;
            dispatch_ex_type <= dispatch_ex_type_n;
            dispatch_op_type <= dispatch_op_type_n;
            dispatch_op_args <= dispatch_op_args_n;
            dispatch_uuid <= dispatch_uuid_n;
            dispatch_wis <= dispatch_wis_n;
            dispatch_PC <= dispatch_PC_n;
            dispatch_is_masked <= dispatch_is_masked_n;
            dispatch_wb <= dispatch_wb_n;
            dispatch_wr_xregs <= dispatch_wr_xregs_n;
            dispatch_rd <= dispatch_rd_n;
            dispatch_bytesel <= dispatch_bytesel_n;
            dispatch_thread_tmask <= dispatch_thread_tmask_n;
            dispatch_vt_tmask <= dispatch_vt_tmask_n;
            dispatch_src_regs <= dispatch_src_regs_n;
            dispatch_used_rs <= dispatch_used_rs_n;
            dispatch_srs_data <= dispatch_srs_data_n;
            dispatch_rs_data <= dispatch_rs_data_n;
            dispatch_sop <= dispatch_sop_n;
            dispatch_eop <= dispatch_eop_n;
        `ifdef EXT_TCU_ENABLE
            wmma_vv_snap_fetch <= wmma_vv_snap_fetch_n;
            wmma_vv_snap_is_b <= wmma_vv_snap_is_b_n;
            wmma_vv_snap_idx <= wmma_vv_snap_idx_n;
            wmma_vv_snap_valid <= wmma_vv_snap_valid_n;
            wmma_vv_a_rows <= wmma_vv_a_rows_n;
            wmma_vv_c_row <= wmma_vv_c_row_n;
            wmma_vv_b_rows <= wmma_vv_b_rows_n;
        `endif
            simd_ctr <= simd_ctr_n;
            sew_ctr <= sew_ctr_n;
            gpr_req_sent <= gpr_req_sent_n;
        end
    end

    // CSRs udpate
    always @(posedge clk) begin
        if (reset) begin
            csr_sew_type <= '0;
            csr_vstate <= '0;
        end else if (soperands_fire && soperands_is_vset) begin
            if (soperands_if.data.op_args.vset.use_zimm) begin
                csr_sew_type <= soperands_if.data.op_args.vset.zimm[3 +: SEW_TYPE_W];
            end else begin
                csr_sew_type <= soperands_if.data.rs2_data[0][3 +: SEW_TYPE_W];
            end
            csr_vstate <= vset_new_state;
        end
    end

    assign dispatch_valid = (state == STATE_DISPATCH);

    // output buffer
    VX_elastic_buffer #(
        .DATAW   (OUT_DATAW),
        .SIZE    (`TO_OUT_BUF_SIZE(OUT_BUF)),
        .OUT_REG (`TO_OUT_BUF_REG(OUT_BUF))
    ) out_buf (
        .clk      (clk),
        .reset    (reset),
        .valid_in (dispatch_valid),
        .ready_in (dispatch_ready),
        .data_in  ({
            dispatch_uuid,
            dispatch_wis,
            simd_ctr,
            dispatch_tmask,
            csr_sew_type,
            sew_ctr,
            sew_masked,
            1'b1,
            dispatch_is_masked,
            dispatch_PC,
            dispatch_wb && dispatch_eop,
            dispatch_wr_xregs,
            dispatch_ex_type,
            dispatch_op_type,
            dispatch_op_args_f,
            dispatch_rd,
            dispatch_bytesel,
            dispatch_rs_data,
            dispatch_sop,
            dispatch_eop
        }),
        .data_out ({
            voperands_if.data.uuid,
            voperands_if.data.wis,
            voperands_if.data.sid,
            voperands_if.data.tmask,
            voperands_if.data.sew.etype,
            voperands_if.data.sew.idx,
            voperands_if.data.sew.masked,
            voperands_if.data.is_rvv,
            voperands_if.data.is_masked,
            voperands_if.data.PC,
            voperands_if.data.wb,
            voperands_if.data.wr_xregs,
            voperands_if.data.ex_type,
            voperands_if.data.op_type,
            voperands_if.data.op_args,
            voperands_if.data.rd,
            voperands_if.data.bytesel,
            voperands_if.data.rs3_data,
            voperands_if.data.rs2_data,
            voperands_if.data.rs1_data,
            voperands_if.data.sop,
            voperands_if.data.eop
        }),
        .valid_out(voperands_if.valid),
        .ready_out(voperands_if.ready)
    );

    assign vpu_seq_opc_if.valid = soperands_fire && soperands_is_vset;
    assign vpu_seq_opc_if.wis   = soperands_if.data.wis[ISSUE_WIS_W-1 -: PER_OPC_NW_W];
    assign vpu_seq_opc_if.data  = vset_new_state;

    assign soperands_if.ready = (state == STATE_IDLE);

endmodule

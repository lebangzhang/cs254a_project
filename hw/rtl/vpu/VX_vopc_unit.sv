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

module VX_vopc_unit import VX_gpu_pkg::*, VX_vpu_pkg::*; #(
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

    input vpu_csrs_t        vpu_csrs_i,
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

    `UNUSED_VAR (writeback_if.data.sop)
    `UNUSED_VAR (vpu_csrs_i)

    wire [SEW_TYPE_W-1:0] csr_sew_type = vpu_csrs_i.vtype.vsew[SEW_TYPE_W-1:0];

    logic [VSIMD_IDX_W-1:0] simd_ctr, simd_ctr_n;
    logic [SEW_IDX_W-1:0] sew_ctr, sew_ctr_n;

    wire gpr_req_valid, gpr_req_ready, gpr_rsp_valid;
    wire [NUM_SRC_OPDS-1:0] gpr_req_inused;
    wire [NUM_SRC_OPDS-1:0][NUM_VREGS_BITS-1:0] gpr_req_rid;
    wire [VGPR_ADDR_WIDTH-1:0] gpr_req_addr;
    wire [NUM_SRC_OPDS-1:0][VT_COUNT-1:0][VL_COUNT-1:0][`XLEN-1:0] gpr_rsp_data;
    logic [NUM_SRC_OPDS-1:0][NUM_REGS_BITS-1:0] src_regs_r;
    logic [NUM_SRC_OPDS-1:0] used_rs_r;

    wire gpr_wb_valid;
    wire [NUM_VREGS_BITS-1:0]  gpr_wb_rid;
    wire [VGPR_ADDR_WIDTH-1:0] gpr_wb_addr;
    wire [VT_COUNT-1:0][VL_COUNT-1:0][`XLEN-1:0] gpr_wb_data;
    wire [VT_COUNT-1:0][VL_COUNT-1:0][XLENB-1:0] gpr_wb_byteen;

    wire [VT_COUNT-1:0][VL_COUNT-1:0][`XLEN-1:0] vmask;

    for (genvar i = 0; i < NUM_SRC_OPDS; ++i) begin : g_src_valid
        assign gpr_req_rid[i] = to_vreg_number(src_regs_r[i]);
        assign gpr_req_inused[i] = used_rs_r[i] && (get_reg_type(src_regs_r[i]) == REG_TYPE_V);
    end

    assign gpr_wb_rid = to_vreg_number(writeback_if.data.rd);

    if (VGPR_ADDR_BITS != 0) begin : g_gpr_addr
        `CONCAT(gpr_req_addr, soperands_r.wis[ISSUE_WIS_W-1 -: PER_OPC_NW_W], simd_ctr, PER_OPC_NW_BITS, VSIMD_IDX_BITS)
        `CONCAT(gpr_wb_addr, writeback_if.data.wis[ISSUE_WIS_W-1 -: PER_OPC_NW_W], writeback_if.data.sid, PER_OPC_NW_BITS, VSIMD_IDX_BITS)
    end else begin : g_gpr_addr_0
        assign gpr_req_addr = '0;
        assign gpr_wb_addr = '0;
    end

    assign gpr_wb_valid = writeback_if.valid && (get_reg_type(writeback_if.data.rd) == REG_TYPE_V);

    logic gpr_req_pending;
    wire gpr_req_fire = gpr_req_valid && gpr_req_ready;

    assign gpr_req_valid = (state == STATE_FETCH) && ~gpr_req_pending;

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
        .TAG_WIDTH (1)
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

    wire [NUM_SRC_OPDS-1:0][VT_COUNT-1:0][VL_COUNT-1:0][`XLEN-1:0] unpacked_data;
    wire [VT_COUNT-1:0][VL_COUNT-1:0] unpacked_vmask;

    wire is_signed = ~insn_is_unsigned(soperands_r.ex_type, soperands_r.op_type);
    wire sew_masked = insn_is_masked(soperands_r.ex_type, soperands_r.op_type);

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
    wire soperands_is_rvv_lsu = soperands_if.data.is_rvv && (soperands_if.data.ex_type == EX_LSU);
    wire is_rvv_lsu = soperands_r.is_rvv && (soperands_r.ex_type == EX_LSU);

    logic dispatch_valid, dispatch_ready;
    logic [`SIMD_WIDTH-1:0] dispatch_tmask, dispatch_tmask_n;
    logic [EX_BITS-1:0] dispatch_ex_type, dispatch_ex_type_n;
    logic [INST_OP_BITS-1:0] dispatch_op_type, dispatch_op_type_n;
    op_args_t dispatch_op_args, dispatch_op_args_n;
    logic dispatch_wb, dispatch_wb_n;
    logic [NUM_REGS_BITS-1:0] dispatch_rd, dispatch_rd_n;
    logic [NUM_SRC_OPDS-1:0][`SIMD_WIDTH-1:0][`XLEN-1:0] dispatch_rs_data, dispatch_rs_data_n;
    logic dispatch_sop, dispatch_sop_n;
    logic dispatch_eop, dispatch_eop_n;

    operands_t soperands_r;
    `UNUSED_VAR (soperands_r)

    logic [STATE_WIDTH-1:0] state, state_n;

    always @(*) begin
        state_n = state;
        simd_ctr_n = simd_ctr;
        sew_ctr_n = sew_ctr;
        dispatch_tmask_n = dispatch_tmask;
        dispatch_ex_type_n = dispatch_ex_type;
        dispatch_op_type_n = dispatch_op_type;
        dispatch_op_args_n = dispatch_op_args;
        dispatch_wb_n = dispatch_wb;
        dispatch_rd_n = dispatch_rd;
        dispatch_rs_data_n = dispatch_rs_data;
        dispatch_sop_n = dispatch_sop;
        dispatch_eop_n = dispatch_eop;

        case (state)
        STATE_IDLE: begin
            if (soperands_fire) begin
                state_n = STATE_FETCH;
                simd_ctr_n = soperands_is_rvv_lsu ? VSIMD_IDX_W'(soperands_if.data.op_args.lsu.group_idx)
                                                   : VSIMD_IDX_W'(VSIMD_COUNT - 1);
                sew_ctr_n  = SEW_IDX_W'(0);
                dispatch_ex_type_n = soperands_if.data.ex_type;
                dispatch_op_type_n = soperands_if.data.op_type;
                dispatch_op_args_n = soperands_if.data.op_args;
                dispatch_wb_n      = soperands_if.data.wb;
                dispatch_rd_n      = soperands_if.data.rd;
                dispatch_sop_n     = 1'b1;
                dispatch_eop_n     = soperands_is_rvv_lsu || (VSIMD_COUNT == 1);
            end
        end
        STATE_FETCH: begin
            if (gpr_rsp_valid) begin
                state_n = STATE_DISPATCH;
            end
            // convert to scalar micro-op
            for (int t = 0; t < VT_COUNT; ++t) begin
                for (int l = 0; l < VL_COUNT; ++l) begin
                    int thread_idx;
                    thread_idx = (simd_ctr * VT_COUNT) + t;
                    if (is_rvv_lsu) begin
                        dispatch_tmask_n[t * VL_COUNT + l] = (| soperands_r.tmask)
                                                           && (sew_masked ? unpacked_vmask[t][l] : 1'b1);
                    end else begin
                        dispatch_tmask_n[t * VL_COUNT + l] = (thread_idx < `NUM_THREADS)
                                                           && soperands_r.tmask[thread_idx]
                                                           && (sew_masked ? unpacked_vmask[t][l] : 1'b1);
                    end
                end
            end
            dispatch_rs_data_n[0] = (used_rs_r[0] && (get_reg_type(src_regs_r[0]) == REG_TYPE_V)) ? unpacked_data[0] : soperands_r.rs1_data;
            dispatch_rs_data_n[1] = (used_rs_r[1] && (get_reg_type(src_regs_r[1]) == REG_TYPE_V)) ? unpacked_data[1] : soperands_r.rs2_data;
            dispatch_rs_data_n[2] = (used_rs_r[2] && (get_reg_type(src_regs_r[2]) == REG_TYPE_V)) ? unpacked_data[2] : soperands_r.rs3_data;
        end
        STATE_DISPATCH: begin
            if (dispatch_ready) begin
                if (is_rvv_lsu || (sew_ctr == 0 && simd_ctr == 0)) begin
                    state_n = STATE_IDLE;
                end else begin
                    if (simd_ctr != 0) begin
                        simd_ctr_n = simd_ctr - 1;
                    end
                    if (sew_ctr != 0) begin
                        sew_ctr_n = sew_ctr - 1;
                    end
                    dispatch_sop_n = 1'b0;
                    dispatch_eop_n = (simd_ctr_n == 0) && (sew_ctr_n == 0);
                    state_n = STATE_FETCH;
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
            gpr_req_pending <= 1'b0;
            soperands_r <= '0;
            src_regs_r <= '0;
            used_rs_r <= '0;
        end else begin
            state <= state_n;
            if (soperands_fire) begin
                soperands_r <= soperands_if.data;
                src_regs_r <= src_regs_i;
                used_rs_r <= used_rs_i;
            end
            dispatch_tmask <= dispatch_tmask_n;
            dispatch_ex_type <= dispatch_ex_type_n;
            dispatch_op_type <= dispatch_op_type_n;
            dispatch_op_args <= dispatch_op_args_n;
            dispatch_wb <= dispatch_wb_n;
            dispatch_rd <= dispatch_rd_n;
            dispatch_rs_data <= dispatch_rs_data_n;
            dispatch_sop <= dispatch_sop_n;
            dispatch_eop <= dispatch_eop_n;
            simd_ctr <= simd_ctr_n;
            sew_ctr <= sew_ctr_n;
            if (gpr_rsp_valid) begin
                gpr_req_pending <= 1'b0;
            end else if (gpr_req_fire) begin
                gpr_req_pending <= 1'b1;
            end
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
            soperands_r.uuid,
            soperands_r.wis,
            simd_ctr,
            dispatch_tmask,
            csr_sew_type,
            sew_ctr,
            sew_masked,
            1'b1,
            soperands_r.PC,
            dispatch_wb,
            soperands_r.wr_xregs,
            dispatch_ex_type,
            dispatch_op_type,
            dispatch_op_args,
            dispatch_rd,
            soperands_r.bytesel,
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

    // TODO:
    assign vpu_seq_opc_if.valid = 'x;
    assign vpu_seq_opc_if.wis   = 'x;
    assign vpu_seq_opc_if.data  = 'x;

    assign soperands_if.ready = (state == STATE_IDLE);

endmodule

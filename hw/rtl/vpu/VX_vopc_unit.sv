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

    logic [ETW_TYPE_W-1:0] csr_etw_type;

    logic [VSIMD_IDX_W-1:0] simd_ctr, simd_ctr_n;
    logic [ETW_IDX_W-1:0] etw_ctr, etw_ctr_n;

    wire gpr_req_valid, gpr_rsp_valid;
    wire [NUM_SRC_OPDS-1:0] gpr_req_inused;
    wire [PER_OPC_NW_BITS-1:0] gpr_req_wis;
    wire [NUM_SRC_OPDS-1:0][NUM_VREGS_BITS-1:0] gpr_req_rid;
    wire [VGPR_ADDR_WIDTH-1:0] gpr_req_addr;
    wire [NUM_SRC_OPDS-1:0][VT_COUNT-1:0][VL_COUNT-1:0][`XLEN-1:0] gpr_rsp_data;

    wire gpr_wb_valid;
    wire [PER_OPC_NW_BITS-1:0] gpr_wb_wis;
    wire [NUM_VREGS_BITS-1:0]  gpr_wb_rid;
    wire [VGPR_ADDR_WIDTH-1:0] gpr_wb_addr;
    wire [VT_COUNT-1:0][VL_COUNT-1:0][`XLEN-1:0] gpr_wb_data;
    wire [VT_COUNT-1:0][VL_COUNT-1:0][XLENB-1:0] gpr_wb_byteen;

    wire [VT_COUNT-1:0][VL_COUNT-1:0][VLENB-1:0] vmask;

    for (genvar i = 0; i < NUM_SRC_OPDS; ++i) begin : g_src_valid
        assign gpr_req_rid[i] = to_vreg_number(src_regs_i[i]);
        assign gpr_req_inused[i] = used_rs_i[i] && (get_reg_type(src_regs_i[i]) == REG_TYPE_V);
    end

    assign gpr_wb_rid = to_vreg_number(writeback_if.data.rd);

    assign gpr_req_wis = scoreboard_if.data.wis[ISSUE_WIS_W-1 -: PER_OPC_NW_BITS];
    assign gpr_wb_wis = writeback_if.data.wis[ISSUE_WIS_W-1 -: PER_OPC_NW_BITS];

    if (VGPR_ADDR_BITS != 0) begin : g_gpr_addr
        if (VSIMD_COUNT != 1) begin : g_gpr_addr_wis_sid
            `CONCAT(gpr_req_addr, gpr_req_wis, simd_ctr, PER_OPC_NW_BITS, SIMD_IDX_W)
            `CONCAT(gpr_wb_addr, gpr_wb_wis, writeback_if.data.sid, PER_OPC_NW_BITS, SIMD_IDX_W)
        end else begin : g_gpr_addr_wis
            assign gpr_req_addr = gpr_req_wis;
            assign gpr_wb_addr = gpr_wb_wis;
        end
    end else begin : g_gpr_addr_0
        assign gpr_req_addr = '0;
        assign gpr_wb_addr = '0;
    end

    assign gpr_wb_valid = writeback_if.valid && (get_reg_type(writeback_if.data.rd) != REG_TYPE_V);

    assign gpr_req_valid = soperands_if.valid;

    wire [VT_COUNT-1:0][VL_COUNT-1:0][`XLEN-1:0] gpr_wb_data_m;
    wire [VT_COUNT-1:0][VL_COUNT-1:0][XLENB-1:0] gpr_wb_byteen_m;

    for (genvar t = 0; t < VT_COUNT; ++t) begin : g_pack
        wire [VL_COUNT-1:0][`XLEN-1:0] gpr_wb_data_um;
        wire [VL_COUNT-1:0][XLENB-1:0] gpr_wb_byteen_um;

        VX_vpu_pack #(
            .NUM_LANES (VL_COUNT)
        ) pack (
            .etw_type (writeback_if.data.etw.etype),
            .etw_idx  (writeback_if.data.etw.idx),
            .data_in  (writeback_if.data.data[VL_COUNT * t +: VL_COUNT]),
            .mask_in  (writeback_if.data.tmask[VL_COUNT * t +: VL_COUNT]),
            .data_out (gpr_wb_data_um),
            .mask_out (gpr_wb_byteen_um)
        );

        VX_vpu_pack_mask #(
            .NUM_LANES (VL_COUNT)
        ) pack_mask (
            .etw_type (writeback_if.data.etw.etype),
            .etw_idx  (writeback_if.data.etw.idx),
            .data_in  (writeback_if.data.data[VL_COUNT * t +: VL_COUNT]),
            .mask_in  (writeback_if.data.tmask[VL_COUNT * t +: VL_COUNT]),
            .data_out (gpr_wb_data_m[t]),
            .mask_out (gpr_wb_byteen_m[t])
        );

        assign gpr_wb_data[t]  = writeback_if.data.etw.masked ? gpr_wb_data_m[t]  : gpr_wb_data_um;
        assign gpr_wb_byteen[t] = writeback_if.data.etw.masked ? gpr_wb_byteen_m[t] : gpr_wb_byteen_um;
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
        `UNUSED_PIN (req_rd_ready),
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
    /*reg [VT_COUNT-1:0][VL_COUNT-1:0][XLENB-1:0] vmask_bank [(PER_ISSUE_WARPS * VSIMD_COUNT)-1:0];
    always @ (posedge clk) begin
        if (writeback_if.valid && writeback_if.data.rd[RV_REGS_BITS-1:0] == '0) begin
            vmasks[gpr_wb_addr] <= gpr_wb_data_m[t];
        end
    end
    wire [VT_COUNT-1:0][VL_COUNT-1:0][VLENB-1:0] vmask = vmask_bank[gpr_req_addr];*/

    wire [NUM_SRC_OPDS-1:0][VT_COUNT-1:0][VL_COUNT-1:0][`XLEN-1:0] unpacked_data;
    wire [VT_COUNT-1:0][VL_COUNT-1:0] unpacked_vmask;

    wire is_signed = ~insn_is_unsigned(soperands_if.data.ex_type, soperands_if.data.op_type);
    wire etw_masked = insn_is_masked(soperands_if.data.ex_type, soperands_if.data.op_type);

    for (genvar t = 0; t < VT_COUNT; ++t) begin : g_unpack
        for (genvar i = 0; i < NUM_SRC_OPDS; ++i) begin : g_i
            VX_vpu_unpack #(
                .NUM_LANES (VL_COUNT)
            ) unpack (
                .etw_type  (csr_etw_type),
                .etw_idx   (etw_ctr),
                .is_signed (is_signed),
                .data_in   (gpr_rsp_data[i][t]),
                .data_out  (unpacked_data[i][t])
            );
        end

        VX_vpu_unpack_mask #(
            .NUM_LANES (VL_COUNT)
        ) unpack_mask (
            .etw_type  (csr_etw_type),
            .etw_idx   (etw_ctr),
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
    logic dispatch_wb, dispatch_wb_n;
    logic [NUM_REGS_BITS-1:0] dispatch_rd, dispatch_rd_n;
    logic [NUM_SRC_OPDS-1:0][`SIMD_WIDTH-1:0][`XLEN-1:0] dispatch_rs_data, dispatch_rs_data_n;
    logic dispatch_sop, dispatch_sop_n;
    logic dispatch_eop, dispatch_eop_n;

    logic [STATE_WIDTH-1:0] state, state_n;

    always @(*) begin
        state_n = state;
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
            end
        end
        STATE_FETCH: begin
            if (gpr_rsp_valid) begin
                state_n = STATE_DISPATCH;
            end
            // convert to scalar micro-op
            for (int t = 0; t < VT_COUNT; ++t) begin
                for (int l = 0; l < VL_COUNT; ++l) begin
                    dispatch_tmask_n[t * VL_COUNT + l] = soperands_if.data.tmask[t] && unpacked_vmask[t][l];
                end
            end
            dispatch_rs_data_n = unpacked_data;
        end
        STATE_DISPATCH: begin
            if (dispatch_ready) begin
                if (etw_ctr == 0 && simd_ctr == 0) begin
                    state_n = STATE_IDLE;
                end else begin
                    if (simd_ctr != 0) begin
                        simd_ctr_n = simd_ctr - 1;
                    end
                    if (etw_ctr != 0) begin
                        etw_ctr_n = etw_ctr - 1;
                    end
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
            etw_ctr <= 0;
        end else begin
            state <= state_n;
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
            etw_ctr <= etw_ctr_n;
        end
    end

    // CSRs udpate
    always @(posedge clk) begin
        if (reset) begin
            csr_etw_type <= '0;
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
            soperands_if.data.uuid,
            soperands_if.data.wis,
            simd_ctr,
            dispatch_tmask,
            csr_etw_type,
            etw_ctr,
            etw_masked,
            soperands_if.data.PC,
            dispatch_wb,
            dispatch_ex_type,
            dispatch_op_type,
            dispatch_op_args,
            dispatch_rd,
            dispatch_rs_data,
            dispatch_sop,
            dispatch_eop
        }),
        .data_out ({
            voperands_if.data.uuid,
            voperands_if.data.wis,
            voperands_if.data.sid,
            voperands_if.data.tmask,
            voperands_if.data.etw.etype,
            voperands_if.data.etw.idx,
            voperands_if.data.etw.masked,
            voperands_if.data.PC,
            voperands_if.data.wb,
            voperands_if.data.ex_type,
            voperands_if.data.op_type,
            voperands_if.data.op_args,
            voperands_if.data.rd,
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
    assign vpu_seq_opc_if.valid = 0;
    assign vpu_seq_opc_if.wis   = '0;
    assign vpu_seq_opc_if.data  = '0;

    assign vopc_operands_if.ready = (state == STATE_IDLE);

endmodule

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

module VX_opc_unit import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = "",
    parameter OUT_BUF = 3
) (
    input wire              clk,
    input wire              reset,

`ifdef PERF_ENABLE
    output wire [PERF_CTR_BITS-1:0] perf_stalls,
`endif

    VX_writeback_if.slave   writeback_if,
    VX_scoreboard_if.slave  scoreboard_if,
    VX_operands_if.master   operands_if
);
    `UNUSED_SPARAM (INSTANCE_ID)

    localparam GPR_FILE_SIZE  = PER_OPC_WARPS * NUM_REGS * `NUM_THREADS * XLENB;
    localparam GPR_DATA_SIZE  = `SIMD_WIDTH * XLENB;
    localparam GPR_DATA_WIDTH = GPR_DATA_SIZE * 8;
    localparam GPR_ENTRIES    = GPR_FILE_SIZE / GPR_DATA_SIZE;
    localparam GPR_ADDR_BITS  = `CLOG2(GPR_ENTRIES / NUM_REGS);
    localparam GPR_ADDR_WIDTH = `UP(GPR_ADDR_BITS);
    localparam GPR_TAG_WIDTH  = UUID_WIDTH + ISSUE_WIS_W + SIMD_IDX_W + `SIMD_WIDTH + PC_BITS + 1 + EX_BITS + INST_OP_BITS + INST_ARGS_BITS + NUM_REGS_BITS + 1 + 1;
    localparam OUT_DATAW      = $bits(operands_t);

    `UNUSED_VAR (writeback_if.data.sop)

    wire [`SIMD_WIDTH-1:0] simd_out;
    wire [SIMD_IDX_W-1:0] simd_pid;
    wire simd_sop, simd_eop;

    wire gpr_req_valid, gpr_rsp_valid;
    wire [NUM_SRC_OPDS-1:0] gpr_req_inused;
    wire [NUM_SRC_OPDS-1:0][NUM_REGS_BITS-1:0] gpr_req_rid;
    wire [GPR_ADDR_WIDTH-1:0] gpr_req_addr;
    wire [GPR_TAG_WIDTH-1:0] gpr_req_tag, gpr_rsp_tag;
    wire [NUM_SRC_OPDS-1:0][GPR_DATA_WIDTH-1:0] gpr_rsp_data;
    wire gpr_req_ready, gpr_rsp_ready;

    wire [GPR_ADDR_WIDTH-1:0] gpr_wb_addr;
    wire [GPR_DATA_SIZE-1:0] gpr_wb_byteen;

    assign gpr_req_rid = {scoreboard_if.data.rs3, scoreboard_if.data.rs2, scoreboard_if.data.rs1};

    for (genvar i = 0; i < NUM_SRC_OPDS; ++i) begin : g_inused
        assign gpr_req_inused[i] = scoreboard_if.data.used_rs[i] && (gpr_req_rid[i] != 0);
    end

    if (GPR_ADDR_BITS != 0) begin : g_gpr_addr
        `CONCAT(gpr_req_addr, scoreboard_if.data.wis[ISSUE_WIS_W-1 -: PER_OPC_NW_W], simd_pid, PER_OPC_NW_BITS, SIMD_IDX_BITS)
        `CONCAT(gpr_wb_addr, writeback_if.data.wis[ISSUE_WIS_W-1 -: PER_OPC_NW_W], writeback_if.data.sid, PER_OPC_NW_BITS, SIMD_IDX_BITS)
    end else begin : g_gpr_addr_0
        assign gpr_req_addr = '0;
        assign gpr_wb_addr = '0;
    end

    for (genvar i = 0; i < `SIMD_WIDTH; ++i) begin : g_gpr_wr_byteen
        assign gpr_wb_byteen[i*XLENB+:XLENB] = {XLENB{writeback_if.data.tmask[i]}};
    end

    assign gpr_req_tag = {
        scoreboard_if.data.uuid,
        scoreboard_if.data.wis,
        simd_pid,
        simd_out,
        scoreboard_if.data.PC,
        scoreboard_if.data.wb,
        scoreboard_if.data.ex_type,
        scoreboard_if.data.op_type,
        scoreboard_if.data.op_args,
        scoreboard_if.data.rd,
        simd_sop,
        simd_eop
    };

    // simd iterator (skip requests with inactive threads)
    VX_nz_iterator #(
        .DATAW (`SIMD_WIDTH),
        .N     (SIMD_COUNT)
    ) simd_iter (
        .clk     (clk),
        .reset   (reset),
        .valid_in(scoreboard_if.valid),
        .data_in (scoreboard_if.data.tmask),
        .next    (gpr_req_ready),
        `UNUSED_PIN (valid_out),
        .data_out(simd_out),
        .pid     (simd_pid),
        .sop     (simd_sop),
        .eop     (simd_eop)
    );

    assign gpr_req_valid = scoreboard_if.valid;
    assign scoreboard_if.ready = gpr_req_ready && simd_eop;

    VX_gpr_file #(
        .FILE_SIZE (GPR_FILE_SIZE),
        .REG_COUNT (NUM_REGS),
        .NUM_REQS  (NUM_SRC_OPDS),
        .NUM_BANKS (`NUM_GPR_BANKS),
        .DATA_SIZE (GPR_DATA_SIZE),
        .TAG_WIDTH (GPR_TAG_WIDTH)
    ) gpr_file (
        .clk          (clk),
        .reset        (reset),
    `ifdef PERF_ENABLE
        .perf_stalls  (perf_stalls),
    `endif
        .req_rd_valid (gpr_req_valid),
        .req_rd_inused(gpr_req_inused),
        .req_rd_rid   (gpr_req_rid),
        .req_rd_addr  (gpr_req_addr),
        .req_rd_tag   (gpr_req_tag),
        .req_rd_ready (gpr_req_ready),
        .rsp_rd_valid (gpr_rsp_valid),
        .rsp_rd_data  (gpr_rsp_data),
        .rsp_rd_tag   (gpr_rsp_tag),
        .rsp_rd_ready (gpr_rsp_ready),
        .req_wr_valid (writeback_if.valid),
        .req_wr_addr  (gpr_wb_addr),
        .req_wr_rid   (writeback_if.data.rd),
        .req_wr_mask  (gpr_wb_byteen),
        .req_wr_data  (writeback_if.data.data)
    );

    // output buffer
    VX_elastic_buffer #(
        .DATAW   (OUT_DATAW),
        .SIZE    (`TO_OUT_BUF_SIZE(OUT_BUF)),
        .OUT_REG (`TO_OUT_BUF_REG(OUT_BUF))
    ) out_buf (
        .clk      (clk),
        .reset    (reset),
        .valid_in (gpr_rsp_valid),
        .ready_in (gpr_rsp_ready),
        .data_in  ({gpr_rsp_tag[GPR_TAG_WIDTH-1:2], // remove sop/eop
                    gpr_rsp_data, // operand data
                    gpr_rsp_tag[1:0]}), // sop/eop
        .data_out ({
            operands_if.data.uuid,
            operands_if.data.wis,
            operands_if.data.sid,
            operands_if.data.tmask,
            operands_if.data.PC,
            operands_if.data.wb,
            operands_if.data.ex_type,
            operands_if.data.op_type,
            operands_if.data.op_args,
            operands_if.data.rd,
            operands_if.data.rs3_data,
            operands_if.data.rs2_data,
            operands_if.data.rs1_data,
            operands_if.data.sop,
            operands_if.data.eop
        }),
        .valid_out(operands_if.valid),
        .ready_out(operands_if.ready)
    );

endmodule

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

// reset all GPRs in debug mode
`ifdef SIMULATION
`ifndef NDEBUG
`define GPR_RESET
`endif
`endif

module VX_gpr_file import VX_gpu_pkg::*; #(
    parameter FILE_SIZE   = 1,
    parameter REG_COUNT   = 1,
    parameter NUM_REQS    = 1,
    parameter NUM_BANKS   = 1,
    parameter DATA_SIZE   = 1,
    parameter TAG_WIDTH   = 1,
    parameter DATA_WIDTH  = DATA_SIZE * 8,
    parameter ENTRIES     = FILE_SIZE / DATA_SIZE,
    parameter ADDR_BITS   = `CLOG2(ENTRIES / REG_COUNT),
    parameter ADDR_WIDTH  = `UP(ADDR_BITS),
    parameter REGID_WIDTH = `CLOG2(REG_COUNT)
) (
    input wire                  clk,
    input wire                  reset,

`ifdef PERF_ENABLE
    output wire [PERF_CTR_BITS-1:0] perf_stalls,
`endif

    input wire                  req_rd_valid,
    input wire [NUM_REQS-1:0]   req_rd_inused,
    input wire [NUM_REQS-1:0][REGID_WIDTH-1:0] req_rd_rid,
    input wire [ADDR_WIDTH-1:0] req_rd_addr,
    input wire [TAG_WIDTH-1:0]  req_rd_tag,
    output wire                 req_rd_ready,

    output wire                 rsp_rd_valid,
    output wire [NUM_REQS-1:0][DATA_WIDTH-1:0] rsp_rd_data,
    output wire [TAG_WIDTH-1:0] rsp_rd_tag,
    input  wire                 rsp_rd_ready,

    input wire                  req_wr_valid,
    input wire [REGID_WIDTH-1:0] req_wr_rid,
    input wire [ADDR_WIDTH-1:0] req_wr_addr,
    input wire [DATA_SIZE-1:0]  req_wr_mask,
    input wire [DATA_WIDTH-1:0] req_wr_data
);
    localparam REQ_SEL_WIDTH   = `CLOG2(NUM_REQS);
    localparam BANK_SEL_BITS   = `CLOG2(NUM_BANKS);
    localparam BANK_SEL_WIDTH  = `UP(BANK_SEL_BITS);
    localparam BANK_ENTRIES    = ENTRIES / NUM_BANKS;
    localparam BANK_ADDR_WIDTH = `CLOG2(BANK_ENTRIES);

    localparam REGID_REM_BITS = REGID_WIDTH - BANK_SEL_BITS;

    wire [NUM_REQS-1:0] xbar_valid_in, xbar_ready_in;
    wire [NUM_REQS-1:0][REGID_REM_BITS-1:0] xbar_data_in;
    wire [NUM_REQS-1:0][BANK_SEL_WIDTH-1:0] xbar_sel_in;

    wire [NUM_BANKS-1:0] xbar_valid_out, xbar_ready_out;
    wire [NUM_BANKS-1:0] req_rd_valid_st1, req_rd_valid_st2;
    wire [NUM_BANKS-1:0][REGID_REM_BITS-1:0] xbar_data_out, req_rd_rgm_st1;
    wire [NUM_BANKS-1:0][DATA_WIDTH-1:0] gpr_rd_data_st2;
    wire [NUM_BANKS-1:0][REQ_SEL_WIDTH-1:0] xbar_sel_out, req_rd_idx_st1, req_rd_idx_st2;

    wire [`UP(ADDR_WIDTH)-1:0] req_rd_addr_st1;
    wire [TAG_WIDTH-1:0] req_rd_tag_st1, req_rd_tag_st2;

    wire pipe_ready_in;
    wire pipe_valid_st1, pipe_ready_st1;
    wire pipe_valid_st2, pipe_ready_st2;

    reg [NUM_REQS-1:0][DATA_WIDTH-1:0] opd_buffer_st2, opd_buffer_n_st2;

    reg [NUM_REQS-1:0] opd_fetched_st1;

    reg has_collision;
    wire has_collision_st1;

    for (genvar i = 0; i < NUM_REQS; ++i) begin : g_gpr_rd_reg
        assign xbar_data_in[i] = req_rd_rid[i][REGID_WIDTH-1 -: REGID_REM_BITS];
    end

    for (genvar i = 0; i < NUM_REQS; ++i) begin : g_req_bank_idx
        if (NUM_BANKS != 1) begin : g_bn
            assign xbar_sel_in[i] = req_rd_rid[i][BANK_SEL_BITS-1:0];
        end else begin : g_b1
            assign xbar_sel_in[i] = '0;
        end
    end

    wire [NUM_REQS-1:0] src_valid;
    for (genvar i = 0; i < NUM_REQS; ++i) begin : g_src_valid
        assign src_valid[i] = req_rd_inused[i] && ~opd_fetched_st1[i];
    end

    assign xbar_valid_in = {NUM_REQS{req_rd_valid}} & src_valid;

    VX_stream_xbar #(
        .NUM_INPUTS  (NUM_REQS),
        .NUM_OUTPUTS (NUM_BANKS),
        .DATAW       (REGID_REM_BITS),
        .ARBITER     ("P"), // use priority arbiter
        .OUT_BUF     (0)    // no output buffering
    ) req_xbar (
        .clk       (clk),
        .reset     (reset),
        `UNUSED_PIN(collisions),
        .valid_in  (xbar_valid_in),
        .data_in   (xbar_data_in),
        .sel_in    (xbar_sel_in),
        .ready_in  (xbar_ready_in),
        .valid_out (xbar_valid_out),
        .data_out  (xbar_data_out),
        .sel_out   (xbar_sel_out),
        .ready_out (xbar_ready_out)
    );

    assign xbar_ready_out = {NUM_BANKS{pipe_ready_in}};

    always @(*) begin
        has_collision = 0;
        for (integer i = 0; i < NUM_REQS; ++i) begin
            for (integer j = 1; j < (NUM_REQS-i); ++j) begin
                has_collision |= src_valid[i]
                              && src_valid[j+i]
                              && (xbar_sel_in[i] == xbar_sel_in[j+i]);
            end
        end
    end

    wire opd_last_fetch = pipe_ready_in && ~has_collision;

    assign req_rd_ready = opd_last_fetch;

    wire pipe_fire_st1 = pipe_valid_st1 && pipe_ready_st1;
    wire pipe_fire_st2 = pipe_valid_st2 && pipe_ready_st2;

    VX_pipe_buffer #(
        .DATAW  (NUM_BANKS * (1 + REGID_REM_BITS + REQ_SEL_WIDTH) + `UP(ADDR_WIDTH) + 1 + TAG_WIDTH),
        .RESETW (1)
    ) pipe_reg1 (
        .clk      (clk),
        .reset    (reset),
        .valid_in (req_rd_valid),
        .ready_in (pipe_ready_in),
        .data_in  ({xbar_valid_out,   xbar_data_out,  xbar_sel_out,   req_rd_addr,     has_collision,     req_rd_tag}),
        .data_out ({req_rd_valid_st1, req_rd_rgm_st1, req_rd_idx_st1, req_rd_addr_st1, has_collision_st1, req_rd_tag_st1}),
        .valid_out(pipe_valid_st1),
        .ready_out(pipe_ready_st1)
    );

    wire [NUM_REQS-1:0] req_fire_in = xbar_valid_in & xbar_ready_in;

    always @(posedge clk) begin
        if (reset || opd_last_fetch) begin
            opd_fetched_st1 <= '0;
        end else begin
            opd_fetched_st1 <= opd_fetched_st1 | req_fire_in;
        end
    end

    wire [BANK_SEL_WIDTH-1:0] gpr_wr_bank_idx;
    if (NUM_BANKS != 1) begin : g_gpr_wr_bank_idx_bn
        assign gpr_wr_bank_idx = req_wr_rid[BANK_SEL_BITS-1:0];
    end else begin : g_gpr_wr_bank_idx_b1
        assign gpr_wr_bank_idx = '0;
    end

    wire [REGID_REM_BITS-1:0] req_wr_rgm = req_wr_rid[REGID_WIDTH-1 -: REGID_REM_BITS];

    wire [BANK_ADDR_WIDTH-1:0] gpr_wr_addr;
    `CONCAT(gpr_wr_addr, req_wr_addr, req_wr_rgm, ADDR_BITS, REGID_REM_BITS)

    // GPR banks
    for (genvar b = 0; b < NUM_BANKS; ++b) begin : g_gpr_rams
        wire gpr_wr_enabled;
        if (BANK_SEL_BITS != 0) begin : g_gpr_wr_enabled_bn
            assign gpr_wr_enabled = req_wr_valid && (gpr_wr_bank_idx == BANK_SEL_BITS'(b));
        end else begin : g_gpr_wr_enabled_b1
            assign gpr_wr_enabled = req_wr_valid;
        end

        wire [BANK_ADDR_WIDTH-1:0] gpr_rd_addr;
        `CONCAT(gpr_rd_addr, req_rd_addr_st1, req_rd_rgm_st1[b], ADDR_BITS, REGID_REM_BITS)

        VX_dp_ram #(
            .DATAW (DATA_WIDTH),
            .SIZE  (BANK_ENTRIES),
            .WRENW (DATA_SIZE),
         `ifdef GPR_RESET
            .RESET_RAM (1),
         `endif
            .OUT_REG (1),
            .RDW_MODE ("R")
        ) gpr_ram (
            .clk   (clk),
            .reset (reset),
            .read  (pipe_fire_st1),
            .wren  (req_wr_mask),
            .write (gpr_wr_enabled),
            .waddr (gpr_wr_addr),
            .wdata (req_wr_data),
            .raddr (gpr_rd_addr),
            .rdata (gpr_rd_data_st2[b])
        );
    end

    wire pipe_valid2_st1 = pipe_valid_st1 && ~has_collision_st1;

    VX_pipe_buffer #(
        .DATAW  (NUM_BANKS * (1 + REQ_SEL_WIDTH) + TAG_WIDTH),
        .RESETW (1)
    ) pipe_reg2 (
        .clk      (clk),
        .reset    (reset),
        .valid_in (pipe_valid2_st1),
        .ready_in (pipe_ready_st1),
        .data_in  ({req_rd_valid_st1, req_rd_idx_st1, req_rd_tag_st1}),
        .data_out ({req_rd_valid_st2, req_rd_idx_st2, req_rd_tag_st2}),
        .valid_out(pipe_valid_st2),
        .ready_out(pipe_ready_st2)
    );

    always @(posedge clk) begin
        if (reset || pipe_fire_st2) begin
            opd_buffer_st2 <= '0; // clear on reset or when data is sent out
        end else begin
            opd_buffer_st2 <= opd_buffer_n_st2;
        end
    end

    always @(*) begin
        opd_buffer_n_st2 = opd_buffer_st2;
        for (integer b = 0; b < NUM_BANKS; ++b) begin
            if (req_rd_valid_st2[b]) begin
                opd_buffer_n_st2[req_rd_idx_st2[b]] = gpr_rd_data_st2[b];
            end
        end
    end

    assign rsp_rd_valid = pipe_valid_st2;
    assign rsp_rd_data  = opd_buffer_n_st2;
    assign rsp_rd_tag   = req_rd_tag_st2;
    assign pipe_ready_st2 = rsp_rd_ready;

`ifdef PERF_ENABLE
    reg [PERF_CTR_BITS-1:0] collisions_r;
    always @(posedge clk) begin
        if (reset) begin
            collisions_r <= '0;
        end else begin
            collisions_r <= collisions_r + PERF_CTR_BITS'(req_rd_valid && pipe_ready_in && has_collision);
        end
    end
    assign perf_stalls = collisions_r;
`endif

endmodule

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

module VX_operands import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = "",
    parameter ISSUE_ID = 0
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
    localparam NUM_OPDS  = NUM_SRC_OPDS + 1;
    localparam SCB_DATAW = UUID_WIDTH + ISSUE_WIS_W + `NUM_THREADS + PC_BITS + EX_BITS + INST_OP_BITS + INST_ARGS_BITS + NUM_OPDS + (REG_IDX_BITS * NUM_OPDS + OPC_INSN_BITS);
    localparam OPD_DATAW = UUID_WIDTH + ISSUE_WIS_W + SIMD_IDX_W + `SIMD_WIDTH + PC_BITS + EX_BITS + INST_OP_BITS + INST_ARGS_BITS + 1 + NR_BITS + (NUM_SRC_OPDS * `SIMD_WIDTH * `XLEN) + 1 + 1;

    VX_gpr_if per_opc_gpr_if[`NUM_OPCS]();
    VX_opc_if per_opc_if[`NUM_OPCS]();
    VX_operands_if per_opc_operands_if[`NUM_OPCS]();

    wire [PER_ISSUE_WARPS-1:0][OPC_INSN_BITS-1:0] inorder_ticks, inorder_tocks;
    wire [PER_ISSUE_WARPS-1:0] inorder_full;

    // collector selection

    reg [`NUM_OPCS-1:0] select_opcs;
    always @(*) begin
        select_opcs = {`NUM_OPCS{~inorder_full[scoreboard_if.data.wis]}};
        // LSU cannot handle consurent LD/ST instructions: always send to collector 0
        if (`NUM_OPCS > 1 && scoreboard_if.data.ex_type == EX_LSU) begin
            for (int i = 0; i < `NUM_OPCS; ++i) begin
                if (i != 0) select_opcs[i] = 0;
            end
        end
        // SFU cannot handle consurent WCTL instructions: always send to collector 1
        if (`NUM_OPCS > 1 && scoreboard_if.data.ex_type == EX_SFU) begin
            for (int i = 0; i < `NUM_OPCS; ++i) begin
                if (i != 1) select_opcs[i] = 0;
            end
        end
    end

    VX_opc_if opc_if();
    assign opc_if.valid = scoreboard_if.valid;
    assign opc_if.data  = {scoreboard_if.data, inorder_ticks[scoreboard_if.data.wis]};
    assign scoreboard_if.ready = opc_if.ready;

    wire opc_fire = opc_if.valid && opc_if.ready;
    wire operands_fire = operands_if.valid && operands_if.ready;

    for (genvar i = 0; i < PER_ISSUE_WARPS; ++i) begin : g_inorder_lock
        VX_ticket_lock #(
            .N (OPC_INSN_COUNT)
        ) inorder_lock (
            .clk        (clk),
            .reset      (reset),
            .aquire_en  (opc_fire && opc_if.data.wis == i),
            .release_en (operands_fire && operands_if.data.eop && operands_if.data.wis == i),
            .acquire_id (inorder_ticks[i]),
            .release_id (inorder_tocks[i]),
            .full       (inorder_full[i]),
            `UNUSED_PIN (empty)
        );
    end

`IGNORE_UNOPTFLAT_BEGIN
    `AOS_TO_ITF (per_opc, per_opc_if, `NUM_OPCS, SCB_DATAW)
`IGNORE_UNOPTFLAT_END

    VX_stream_arb #(
        .NUM_INPUTS  (1),
        .NUM_OUTPUTS (`NUM_OPCS),
        .DATAW       (SCB_DATAW),
        .ARBITER     ("P"),
        .OUT_BUF     (0)
    ) input_arb (
        .clk       (clk),
        .reset     (reset),
        .valid_in  (opc_if.valid),
        .data_in   (opc_if.data),
        .ready_in  (opc_if.ready),
        .valid_out (per_opc_valid),
        .data_out  (per_opc_data),
        .ready_out (per_opc_ready & select_opcs),
        `UNUSED_PIN(sel_out)
    );

    for (genvar i = 0; i < `NUM_OPCS; ++i) begin : g_collectors
        VX_opc_unit #(
            .INSTANCE_ID (`SFORMATF(("%s-collector%0d", INSTANCE_ID, i))),
            .ISSUE_ID (ISSUE_ID)
        ) opc_unit (
            .clk          (clk),
            .reset        (reset),
            .dep_id       (inorder_tocks),
            .opc_if       (per_opc_if[i]),
            .gpr_if       (per_opc_gpr_if[i]),
            .operands_if  (per_opc_operands_if[i])
        );
    end

    VX_gpr_unit #(
        .INSTANCE_ID (`SFORMATF(("%s-gpr", INSTANCE_ID))),
        .NUM_REQS    (`NUM_OPCS),
        .NUM_BANKS   (`NUM_GPR_BANKS)
    ) gpr_unit (
        .clk          (clk),
        .reset        (reset),
    `ifdef PERF_ENABLE
        .perf_stalls  (perf_stalls),
    `endif
        .writeback_if (writeback_if),
        .gpr_if       (per_opc_gpr_if)
    );

    `ITF_TO_AOS (per_opc_operands_if, per_opc_operands, `NUM_OPCS, OPD_DATAW)

    VX_stream_arb #(
        .NUM_INPUTS  (`NUM_OPCS),
        .NUM_OUTPUTS (1),
        .DATAW       (OPD_DATAW),
        .ARBITER     ("P"),
        .OUT_BUF     (3)
    ) output_arb (
        .clk       (clk),
        .reset     (reset),
        .valid_in  (per_opc_operands_valid),
        .data_in   (per_opc_operands_data),
        .ready_in  (per_opc_operands_ready),
        .valid_out (operands_if.valid),
        .data_out  (operands_if.data),
        .ready_out (operands_if.ready),
        `UNUSED_PIN(sel_out)
    );

endmodule

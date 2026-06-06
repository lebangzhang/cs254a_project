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

module VX_scoreboard import VX_gpu_pkg::*
`ifdef EXT_TCU_ENABLE
    , VX_tcu_pkg::*
`endif
; #(
    parameter `STRING INSTANCE_ID = "",
    parameter ISSUE_ID = 0
) (
    input wire              clk,
    input wire              reset,

`ifdef PERF_ENABLE
    output reg [PERF_CTR_BITS-1:0] perf_stalls,
`endif

    VX_writeback_if.slave   writeback_if,
    VX_ibuffer_if.slave     ibuffer_if [PER_ISSUE_WARPS],
    VX_scoreboard_if.master scoreboard_if
);
    `UNUSED_SPARAM (INSTANCE_ID)
    `UNUSED_PARAM (ISSUE_ID)
    `UNUSED_VAR (writeback_if.data.sop)

    localparam NUM_OPDS  = NUM_SRC_OPDS + 1;
    localparam IN_DATAW  = $bits(ibuffer_t);
    localparam OUT_DATAW = $bits(scoreboard_t) - ISSUE_WIS_W;
`ifdef EXT_TCU_ENABLE
`ifdef EXT_V_ENABLE
    localparam WMMA_VV_TILE_M = TCU_TC_M * TCU_M_STEPS;
    localparam WMMA_VV_TILE_K = TCU_TC_K * TCU_K_STEPS;
    localparam WMMA_VV_UOPS = TCU_TC_M * TCU_M_STEPS * TCU_N_STEPS * TCU_K_STEPS;
    localparam WMMA_VV_UOPS_W = `UP($clog2(WMMA_VV_UOPS + 1));
`endif
`endif

`ifdef EXT_TCU_ENABLE
`ifdef EXT_V_ENABLE
    function automatic logic wmma_vv_is_uop(input ibuffer_t ibuf);
        wmma_vv_is_uop = (ibuf.op_type == INST_TCU_WMMA_VV);
    endfunction

    function automatic logic wmma_vv_is_first_uop(input ibuffer_t ibuf);
        wmma_vv_is_first_uop = (ibuf.op_args.tcu.step_m == 4'(0))
                            && (ibuf.op_args.tcu.step_n == 4'(0))
                            && (ibuf.op_args.tcu.step_k == 4'(0))
                            && (ibuf.op_args.tcu.fmt_d  == 4'(0));
    endfunction

    function automatic logic wmma_vv_is_last_uop(input ibuffer_t ibuf);
        wmma_vv_is_last_uop = (ibuf.op_args.tcu.step_m == 4'(TCU_M_STEPS - 1))
                           && (ibuf.op_args.tcu.step_n == 4'(TCU_N_STEPS - 1))
                           && (ibuf.op_args.tcu.step_k == 4'(TCU_K_STEPS - 1))
                           && (ibuf.op_args.tcu.fmt_d  == 4'(TCU_TC_M - 1));
    endfunction

    function automatic [RV_REGS_BITS-1:0] wmma_vv_base_row(input logic [NUM_REGS_BITS-1:0] reg_num, input op_args_t op_args);
        wmma_vv_base_row = get_reg_idx(reg_num) - RV_REGS_BITS'(int'(op_args.tcu.step_m) * TCU_TC_M + int'(op_args.tcu.fmt_d));
    endfunction

    function automatic [RV_REGS_BITS-1:0] wmma_vv_base_k(input logic [NUM_REGS_BITS-1:0] reg_num, input op_args_t op_args);
        wmma_vv_base_k = get_reg_idx(reg_num) - RV_REGS_BITS'(int'(op_args.tcu.step_k) * TCU_TC_K);
    endfunction

    function automatic [RV_REGS-1:0] wmma_vv_group_mask_vec(input logic [RV_REGS_BITS-1:0] base_idx, input int rows);
        logic [RV_REGS-1:0] mask;
        mask = '0;
        for (int i = 0; i < rows; ++i) begin
            mask[base_idx + RV_REGS_BITS'(i)] = 1'b1;
        end
        return mask;
    endfunction
`endif
`endif

    VX_ibuffer_if staging_if [PER_ISSUE_WARPS]();
    wire [PER_ISSUE_WARPS-1:0] operands_ready;

`ifdef PERF_ENABLE
    wire [PER_ISSUE_WARPS-1:0] stg_valid_in;
    for (genvar w = 0; w < PER_ISSUE_WARPS; ++w) begin : g_stg_valid_in
        assign stg_valid_in[w] = staging_if[w].valid;
    end

    wire perf_stall_per_cycle = |(stg_valid_in & ~operands_ready);

    always @(posedge clk) begin : g_perf_stalls
        if (reset) begin
            perf_stalls <= '0;
        end else begin
            perf_stalls <= perf_stalls + PERF_CTR_BITS'(perf_stall_per_cycle);
        end
    end
`endif

    for (genvar w = 0; w < PER_ISSUE_WARPS; ++w) begin : g_stanging_bufs
        VX_pipe_buffer #(
            .DATAW (IN_DATAW)
        ) stanging_buf (
            .clk      (clk),
            .reset    (reset),
            .valid_in (ibuffer_if[w].valid),
            .data_in  (ibuffer_if[w].data),
            .ready_in (ibuffer_if[w].ready),
            .valid_out(staging_if[w].valid),
            .data_out (staging_if[w].data),
            .ready_out(staging_if[w].ready)
        );
    end

    for (genvar w = 0; w < PER_ISSUE_WARPS; ++w) begin : g_scoreboard
        reg [NUM_REGS-1:0] inuse_regs, inuse_regs_n;
        reg [NUM_XREGS-1:0] inuse_xregs, inuse_xregs_n;
    `ifdef EXT_TCU_ENABLE
    `ifdef EXT_V_ENABLE
        reg wmma_vv_busy, wmma_vv_busy_n;
        reg wmma_vv_emit_active, wmma_vv_emit_active_n;
        reg [PC_BITS-1:0] wmma_vv_pc, wmma_vv_pc_n;
        reg [WMMA_VV_UOPS_W-1:0] wmma_vv_wb_left, wmma_vv_wb_left_n;
        reg [RV_REGS-1:0] wmma_vv_group_mask_vec_r, wmma_vv_group_mask_vec_n;
        reg [RV_REGS-1:0] wmma_vv_dst_mask_vec_r, wmma_vv_dst_mask_vec_n;
    `endif
    `endif
        wire [NUM_OPDS-1:0] operands_busy;

        wire ibuffer_fire = ibuffer_if[w].valid && ibuffer_if[w].ready;
        wire staging_fire = staging_if[w].valid && staging_if[w].ready;

        wire writeback_fire = writeback_if.valid
                           && (writeback_if.data.wis == ISSUE_WIS_W'(w))
                           && writeback_if.data.eop;

        wire [NUM_OPDS-1:0] [NUM_REGS_BITS-1:0] ibf_opds, stg_opds;
        assign ibf_opds = {ibuffer_if[w].data.rs3, ibuffer_if[w].data.rs2, ibuffer_if[w].data.rs1, ibuffer_if[w].data.rd};
        assign stg_opds = {staging_if[w].data.rs3, staging_if[w].data.rs2, staging_if[w].data.rs1, staging_if[w].data.rd};

        wire [NUM_OPDS-1:0] ibf_used_rs = {ibuffer_if[w].data.used_rs, ibuffer_if[w].data.wb};
        wire [NUM_OPDS-1:0] stg_used_rs = {staging_if[w].data.used_rs, staging_if[w].data.wb};

    `ifdef EXT_TCU_ENABLE
    `ifdef EXT_V_ENABLE
        wire ibf_is_wmma_vv = wmma_vv_is_uop(ibuffer_if[w].data);
        wire stg_is_wmma_vv = wmma_vv_is_uop(staging_if[w].data);
        wire ibf_is_wmma_vv_first = ibf_is_wmma_vv && wmma_vv_is_first_uop(ibuffer_if[w].data);
        wire stg_is_wmma_vv_first = stg_is_wmma_vv && wmma_vv_is_first_uop(staging_if[w].data);
        wire curr_is_wmma_vv = ibuffer_fire ? ibf_is_wmma_vv : stg_is_wmma_vv;
        wire curr_is_wmma_cont = curr_is_wmma_vv && wmma_vv_emit_active;
        wire curr_is_wmma_vv_first = ibuffer_fire ? ibf_is_wmma_vv_first : stg_is_wmma_vv_first;

        wire [RV_REGS-1:0] ibf_wmma_issue_mask =
              wmma_vv_group_mask_vec(wmma_vv_base_row(ibuffer_if[w].data.rd,  ibuffer_if[w].data.op_args), WMMA_VV_TILE_M)
            | wmma_vv_group_mask_vec(wmma_vv_base_row(ibuffer_if[w].data.rs1, ibuffer_if[w].data.op_args), WMMA_VV_TILE_M)
            | wmma_vv_group_mask_vec(wmma_vv_base_k  (ibuffer_if[w].data.rs2, ibuffer_if[w].data.op_args), WMMA_VV_TILE_K);
        wire [RV_REGS-1:0] stg_wmma_issue_mask =
              wmma_vv_group_mask_vec(wmma_vv_base_row(staging_if[w].data.rd,  staging_if[w].data.op_args), WMMA_VV_TILE_M)
            | wmma_vv_group_mask_vec(wmma_vv_base_row(staging_if[w].data.rs1, staging_if[w].data.op_args), WMMA_VV_TILE_M)
            | wmma_vv_group_mask_vec(wmma_vv_base_k  (staging_if[w].data.rs2, staging_if[w].data.op_args), WMMA_VV_TILE_K);
        wire [RV_REGS-1:0] curr_wmma_issue_mask = ibuffer_fire ? ibf_wmma_issue_mask : stg_wmma_issue_mask;
    `endif
    `endif

        // Special-register dependency masks
        wire [NUM_XREGS-1:0] ibf_xregs_mask = ibuffer_if[w].data.rd_xregs | ibuffer_if[w].data.wr_xregs;
        wire [NUM_XREGS-1:0] stg_xregs_mask = staging_if[w].data.rd_xregs | staging_if[w].data.wr_xregs;

        wire [NUM_OPDS-1:0][REG_TYPES-1:0][RV_REGS-1:0] ibf_opd_mask, stg_opd_mask;

        for (genvar i = 0; i < NUM_OPDS; ++i) begin : g_opd_masks
            for (genvar j = 0; j < REG_TYPES; ++j) begin : g_j
                assign ibf_opd_mask[i][j] = (1 << get_reg_idx(ibf_opds[i])) & {RV_REGS{ibf_used_rs[i] && get_reg_type(ibf_opds[i]) == j}};
                assign stg_opd_mask[i][j] = (1 << get_reg_idx(stg_opds[i])) & {RV_REGS{stg_used_rs[i] && get_reg_type(stg_opds[i]) == j}};
            end
        end

        always @(*) begin
            inuse_regs_n  = inuse_regs;
            inuse_xregs_n = inuse_xregs;
        `ifdef EXT_TCU_ENABLE
        `ifdef EXT_V_ENABLE
            wmma_vv_busy_n = wmma_vv_busy;
            wmma_vv_emit_active_n = wmma_vv_emit_active;
            wmma_vv_pc_n = wmma_vv_pc;
            wmma_vv_wb_left_n = wmma_vv_wb_left;
            wmma_vv_group_mask_vec_n = wmma_vv_group_mask_vec_r;
            wmma_vv_dst_mask_vec_n = wmma_vv_dst_mask_vec_r;
        `endif
        `endif
            if (writeback_fire) begin
                if (writeback_if.data.wb) begin
                    inuse_regs_n[writeback_if.data.rd] = 0; // release rd
                end
                inuse_xregs_n &= ~writeback_if.data.wr_xregs; // release special regs
            `ifdef EXT_TCU_ENABLE
            `ifdef EXT_V_ENABLE
                if (wmma_vv_busy
                 && writeback_if.data.wb
                 && (get_reg_type(writeback_if.data.rd) == REG_TYPE_V)
                 && (writeback_if.data.PC == wmma_vv_pc)
                 && wmma_vv_dst_mask_vec_r[get_reg_idx(writeback_if.data.rd)]) begin
                    if (wmma_vv_wb_left == WMMA_VV_UOPS_W'(1)) begin
                        wmma_vv_busy_n = 1'b0;
                        wmma_vv_wb_left_n = '0;
                        wmma_vv_group_mask_vec_n = '0;
                        wmma_vv_dst_mask_vec_n = '0;
                    end else begin
                        wmma_vv_wb_left_n = wmma_vv_wb_left - WMMA_VV_UOPS_W'(1);
                    end
                end
            `endif
            `endif
            end
            if (staging_fire) begin
                if (staging_if[w].data.wb) begin
                    inuse_regs_n |= stg_opd_mask[0]; // reserve rd
                end
                inuse_xregs_n |= staging_if[w].data.wr_xregs; // reserve special regs
            `ifdef EXT_TCU_ENABLE
            `ifdef EXT_V_ENABLE
                if (stg_is_wmma_vv) begin
                    if (!wmma_vv_busy && wmma_vv_is_first_uop(staging_if[w].data)) begin
                        wmma_vv_busy_n = 1'b1;
                        wmma_vv_emit_active_n = 1'b1;
                        wmma_vv_pc_n = staging_if[w].data.PC;
                        wmma_vv_wb_left_n = WMMA_VV_UOPS_W'(WMMA_VV_UOPS);
                        wmma_vv_group_mask_vec_n =
                              wmma_vv_group_mask_vec(wmma_vv_base_row(staging_if[w].data.rd,  staging_if[w].data.op_args), WMMA_VV_TILE_M)
                            | wmma_vv_group_mask_vec(wmma_vv_base_row(staging_if[w].data.rs1, staging_if[w].data.op_args), WMMA_VV_TILE_M)
                            | wmma_vv_group_mask_vec(wmma_vv_base_k  (staging_if[w].data.rs2, staging_if[w].data.op_args), WMMA_VV_TILE_K);
                        wmma_vv_dst_mask_vec_n =
                            wmma_vv_group_mask_vec(wmma_vv_base_row(staging_if[w].data.rd, staging_if[w].data.op_args), WMMA_VV_TILE_M);
                    end
                    if (wmma_vv_emit_active && wmma_vv_is_last_uop(staging_if[w].data)) begin
                        wmma_vv_emit_active_n = 1'b0;
                    end
                end
            `endif
            `endif
            end
        end

        wire [REG_TYPES-1:0][RV_REGS-1:0] in_use_mask;
        for (genvar i = 0; i < REG_TYPES; ++i) begin : g_in_use_mask
            wire [RV_REGS-1:0] ibf_reg_mask = ibf_opd_mask[0][i] | ibf_opd_mask[1][i] | ibf_opd_mask[2][i] | ibf_opd_mask[3][i];
            wire [RV_REGS-1:0] stg_reg_mask = stg_opd_mask[0][i] | stg_opd_mask[1][i] | stg_opd_mask[2][i] | stg_opd_mask[3][i];
            wire [RV_REGS-1:0] regs_mask = ibuffer_fire ? ibf_reg_mask : stg_reg_mask;
        `ifdef EXT_TCU_ENABLE
        `ifdef EXT_V_ENABLE
            wire [RV_REGS-1:0] wmma_vv_mask = ((i == REG_TYPE_V) && wmma_vv_busy && !curr_is_wmma_cont)
                                            ? wmma_vv_group_mask_vec_r
                                            : '0;
            assign in_use_mask[i] = (inuse_regs_n[i * RV_REGS +: RV_REGS] | wmma_vv_mask) & regs_mask;
        `else
            assign in_use_mask[i] = inuse_regs_n[i * RV_REGS +: RV_REGS] & regs_mask;
        `endif
        `endif
        end

        wire [REG_TYPES-1:0] regs_busy;
        for (genvar i = 0; i < REG_TYPES; ++i) begin : g_regs_busy
            assign regs_busy[i] = (in_use_mask[i] != 0);
        end

        for (genvar i = 0; i < NUM_OPDS; ++i) begin : g_operands_busy
            wire [REG_TYPE_BITS-1:0] rtype = get_reg_type(stg_opds[i]);
            assign operands_busy[i] = (in_use_mask[rtype] & stg_opd_mask[i][rtype]) != 0;
        end


        wire [NUM_XREGS-1:0] xregs_mask = ibuffer_fire ? ibf_xregs_mask : stg_xregs_mask;
        wire [NUM_XREGS-1:0] xregs_busy = inuse_xregs_n & xregs_mask;
    `ifdef EXT_TCU_ENABLE
    `ifdef EXT_V_ENABLE
        wire wmma_vv_issue_busy = curr_is_wmma_vv_first
                               && ((inuse_regs_n[REG_TYPE_V * RV_REGS +: RV_REGS] & curr_wmma_issue_mask) != 0);
    `else
        wire wmma_vv_issue_busy = 1'b0;
    `endif
    `endif

        reg operands_ready_r;

        always @(posedge clk) begin
            if (reset) begin
                inuse_regs  <= '0;
                inuse_xregs <= '0;
            `ifdef EXT_TCU_ENABLE
            `ifdef EXT_V_ENABLE
                wmma_vv_busy <= 1'b0;
                wmma_vv_emit_active <= 1'b0;
                wmma_vv_pc <= '0;
                wmma_vv_wb_left <= '0;
                wmma_vv_group_mask_vec_r <= '0;
                wmma_vv_dst_mask_vec_r <= '0;
            `endif
            `endif
            end else begin
                inuse_regs <= inuse_regs_n;
                inuse_xregs <= inuse_xregs_n;
            `ifdef EXT_TCU_ENABLE
            `ifdef EXT_V_ENABLE
                wmma_vv_busy <= wmma_vv_busy_n;
                wmma_vv_emit_active <= wmma_vv_emit_active_n;
                wmma_vv_pc <= wmma_vv_pc_n;
                wmma_vv_wb_left <= wmma_vv_wb_left_n;
                wmma_vv_group_mask_vec_r <= wmma_vv_group_mask_vec_n;
                wmma_vv_dst_mask_vec_r <= wmma_vv_dst_mask_vec_n;
            `endif
            `endif
            end
            operands_ready_r <= (regs_busy == 0) && (xregs_busy == 0) && ~wmma_vv_issue_busy;
        end

        assign operands_ready[w] = operands_ready_r;

    `ifdef SIMULATION
        reg [31:0] timeout_ctr;

        always @(posedge clk) begin
            if (reset) begin
                timeout_ctr <= '0;
            end else begin
                if (staging_if[w].valid && ~staging_if[w].ready) begin
                `ifdef DBG_TRACE_PIPELINE
                    `TRACE(4, ("%t: *** %s-stall: wid=%0d, PC=0x%0h, tmask=%b, cycles=%0d, opds_busy=%b, xregs_busy=%b (#%0d)\n",
                        $time, INSTANCE_ID, w, to_fullPC(staging_if[w].data.PC), staging_if[w].data.tmask, timeout_ctr,
                        operands_busy, xregs_busy, staging_if[w].data.uuid))
                `endif
                    timeout_ctr <= timeout_ctr + 1;
                end else if (ibuffer_fire) begin
                    timeout_ctr <= '0;
                end
            end
        end

        `RUNTIME_ASSERT((timeout_ctr < STALL_TIMEOUT),
            ("timeout: wid=%0d, PC=0x%0h, tmask=%b, cycles=%0d, inuse=%b (#%0d)",
                w, to_fullPC(staging_if[w].data.PC), staging_if[w].data.tmask, timeout_ctr,
                operands_busy, staging_if[w].data.uuid))

        `RUNTIME_ASSERT(~(writeback_fire && writeback_if.data.wb) || inuse_regs[writeback_if.data.rd] != 0,
            ("invalid writeback register: wid=%0d, PC=0x%0h, tmask=%b, rd=%0d (#%0d)",
                w, to_fullPC(writeback_if.data.PC), writeback_if.data.tmask, writeback_if.data.rd, writeback_if.data.uuid))

        `RUNTIME_ASSERT(~writeback_fire || ((writeback_if.data.wr_xregs & ~inuse_xregs) == '0),
            ("invalid writeback special register: wid=%0d, PC=0x%0h, tmask=%b, xregs=%b (#%0d)",
                w, to_fullPC(writeback_if.data.PC), writeback_if.data.tmask, writeback_if.data.wr_xregs, writeback_if.data.uuid))
    `endif

    end

    wire [PER_ISSUE_WARPS-1:0] arb_valid_in;
    wire [PER_ISSUE_WARPS-1:0][OUT_DATAW-1:0] arb_data_in;
    wire [PER_ISSUE_WARPS-1:0] arb_ready_in;

    for (genvar w = 0; w < PER_ISSUE_WARPS; ++w) begin : g_arb_data_in
        assign arb_valid_in[w] = staging_if[w].valid && operands_ready[w];
        assign arb_data_in[w] = {
            staging_if[w].data.uuid,
            staging_if[w].data.tmask,
            staging_if[w].data.PC,
            staging_if[w].data.ex_type,
            staging_if[w].data.op_type,
            staging_if[w].data.op_args,
        `ifdef EXT_V_ENABLE
            staging_if[w].data.is_rvv,
            staging_if[w].data.is_masked,
        `endif
            staging_if[w].data.wb,
            staging_if[w].data.wr_xregs,
            staging_if[w].data.used_rs,
            staging_if[w].data.rd,
            staging_if[w].data.bytesel,
            staging_if[w].data.rs1,
            staging_if[w].data.rs2,
            staging_if[w].data.rs3
        };
        assign staging_if[w].ready = arb_ready_in[w] && operands_ready[w];
    end

    VX_stream_arb #(
        .NUM_INPUTS (PER_ISSUE_WARPS),
        .DATAW      (OUT_DATAW),
        .ARBITER    ("C"),
        .OUT_BUF    (3)
    ) out_arb (
        .clk      (clk),
        .reset    (reset),
        .valid_in (arb_valid_in),
        .ready_in (arb_ready_in),
        .data_in  (arb_data_in),
        .data_out ({
            scoreboard_if.data.uuid,
            scoreboard_if.data.tmask,
            scoreboard_if.data.PC,
            scoreboard_if.data.ex_type,
            scoreboard_if.data.op_type,
            scoreboard_if.data.op_args,
        `ifdef EXT_V_ENABLE
            scoreboard_if.data.is_rvv,
            scoreboard_if.data.is_masked,
        `endif
            scoreboard_if.data.wb,
            scoreboard_if.data.wr_xregs,
            scoreboard_if.data.used_rs,
            scoreboard_if.data.rd,
            scoreboard_if.data.bytesel,
            scoreboard_if.data.rs1,
            scoreboard_if.data.rs2,
            scoreboard_if.data.rs3
        }),
        .valid_out (scoreboard_if.valid),
        .ready_out (scoreboard_if.ready),
        .sel_out   (scoreboard_if.data.wis)
    );

endmodule

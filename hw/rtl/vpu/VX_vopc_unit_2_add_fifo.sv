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

module VX_vopc_unit import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = "",
    parameter ISSUE_ID = 0
) (
    input wire              clk,
    input wire              reset,

    input wire [`UP(`NUM_OPCS-1)-1:0][ISSUE_WIS_W-1:0] pending_wis_in,
    input reg [`UP(`NUM_OPCS-1)-1:0][NUM_REGS-1:0] pending_regs_in,

    output wire [ISSUE_WIS_W-1:0] pending_wis,
    output wire [NUM_REGS-1:0] pending_regs,


    // Scoreboard Interface 
    VX_scoreboard_if.slave  scoreboard_if,

    // Writeback Interface 
    VX_writeback_if.slave   writeback_if,

    // General Purpose Reg File
    VX_gpr_if.master        gpr_if,

    // Vector Reg File 
    VX_vgpr_if.master       vgpr_if,

    // To Dispatch Unit
    VX_operands_if.master   operands_if
);
    `UNUSED_SPARAM (INSTANCE_ID)
    `UNUSED_PARAM (ISSUE_ID)

    localparam NUM_OPDS = NUM_SRC_OPDS + 1;
    localparam SCB_DATAW = UUID_WIDTH + ISSUE_WIS_W + `NUM_THREADS + PC_BITS + EX_BITS + INST_OP_BITS + INST_ARGS_BITS + NUM_OPDS + (NUM_OPDS * REG_IDX_BITS);
    localparam OUT_DATAW = UUID_WIDTH + ISSUE_WIS_W + SIMD_IDX_W + `SIMD_WIDTH + PC_BITS + EX_BITS + INST_OP_BITS + INST_ARGS_BITS + 1 + NR_BITS + (NUM_SRC_OPDS * `SIMD_WIDTH * `XLEN) + 1 + 1;

    localparam STATE_IDLE     = 0;
    localparam STATE_FETCH    = 1;
    localparam STATE_DISPATCH = 2;



    VX_scoreboard_if staging_if();

    reg [NUM_SRC_OPDS-1:0] opds_needed, opds_needed_n;
    reg [NUM_SRC_OPDS-1:0] opds_busy, opds_busy_n;
    reg [2:0] state, state_n;
    reg state_reduce, state_reduce_n; //Added 
    wire output_ready;

    wire [`SIMD_WIDTH-1:0] simd_out;
    wire [SIMD_IDX_W-1:0] simd_pid;
    wire simd_sop, simd_eop;


    // ** SubModule 1 : Handle Scoreboard Interface **
    VX_pipe_buffer #(
        .DATAW (SCB_DATAW)
    ) stanging_buf (
        .clk      (clk),
        .reset    (reset),
        .valid_in (scoreboard_if.valid),
        .data_in  (scoreboard_if.data),
        .ready_in (scoreboard_if.ready),
        .valid_out(staging_if.valid),
        .data_out (staging_if.data),
        .ready_out(staging_if.ready)
    );

    // Dest Register Number 
    wire [NR_BITS-1:0] rd = to_reg_number(staging_if.data.rd);

    // Assume: Only Vector Inputs 
    // --> Assume: Collector Selection by voperands 
    // --> Assume: No Permutation Insn 
    wire [NR_BITS-1:0] rs1 = to_reg_number(staging_if.data.rd);
    wire [NR_BITS-1:0] rs2 = to_reg_number(staging_if.data.rd);
    wire [NR_BITS-1:0] rs3 = to_reg_number(staging_if.data.rd);

    // Identify type of instruction 
    // Note: This is not a good way of identifying reduce 
    // ==> Need to fix, not sure how, prob need metadata from decode ?
    // wire is_reduce_instruction = (staging_if.data.rd.rtype == 1);
    // Somehow
    wire is_reduction_instruction = ____;  



    // ** SubModule 2 : Handle Writeback Interface + Choose Interface **
    // Check if is reduction 
    // NOTE: Need a better way of determining is_reduction_signal (if
    // permutation is also considered)

    // FIFO 
    // NOTE: To switch to a proper VX_fifo 
    // TO FIX to a proper structure + Check for fullness ****************
    VX_writeback_if.data wb_fifo_reduce [15:0];
    logic [3:0] wb_fifo_head_ptr, wb_fifo_tail_ptr; 
    logic [3:0] wb_fifo_write, wb_fifo_dequeue;
    logic [UUID_WIDTH-1:0] wb_fifo_uuid, wb_fifo_uuid_prev;

    always @(posedge clk) begin 
        wb_fifo_uuid_prev <= wb_fifo_uuid;
        wb_fifo_uuid      <= writeback_if.data.uuid;

        if(reset) begin 
            wb_fifo_head_ptr <= 0;
            wb_fifo_tail_ptr <= 0;
        end
        else begin 
            if(is_reduction_signal && (wb_fifo_uuid_prev != wb_fifo_uuid) ) begin 
                wb_fifo_reduce [tail_ptr] <= writeback_if.data;
                tail_ptr <= tail_ptr + 1;
            end

            if(wb_fifo_dequeue) begin 
                head_ptr <= head_ptr + 1;
            end
        end 
    end 



    // ** SubModule 3 : Request and Response fire signals ** 
    // GP Reg File: Fire Request + Fire Response 
    wire gpr_req_fire = gpr_if.req_valid && gpr_if.req_ready;
    wire gpr_rsp_fire = gpr_if.rsp_valid;

    // Vec Reg File: Fire Request + Fire Response 
    wire vgpr_req_fire = vgpr_if.req_valid && vgpr_if.req_ready;
    wire vgpr_rsp_fire = vgpr_if.rsp_valid;



    // ** SubModule 4 : Ready to Dispatch --> Dequeue from SB **
    // Don't Dequeue if is a writeback signal
    wire dequeue = (state == STATE_DISPATCH) && output_ready;
    assign staging_if.ready = dequeue && simd_eop;



   
    // ** SubModule 5 : Writeback Reduction Servicing **
    // Reduction info 
    reg [31:0][NUM_SRC_OPDS - 1 : 0] reduction_counter;
    reg [31:0][NUM_SRC_OPDS - 1 : 0] reduction_src;

    always @(posedge clk) begin 
        if(reset) begin
            for(genvar i = 0; i < 32; ++i) begin : reduction_info_reset 
                reduction_counter[i]  <= '0;
                reduction_src[i]      <= '0;
            end 
        end
    end

    wire head = wb_fifo_reduce[head_ptr];
    wire wb_datatype = writeback_if.data.rd[NR_BITS      - 1 : RV_REGS_BITS];
    wire wb_rd_id    = writeback_if.data.rd[RV_REGS_BITS - 1 : 0]; 
    wire is_reduction_signal = writeback_if.valid && _______; 

    wire wb_fifo_empty = (head_ptr == tail_ptr);
    // Check if need to service
    wire reduction_service_required = (!wb_fifo_empty) && (reduction_counter[wb_rd_id] != '0)  







    // ** SubModule 6 : Only Get required src operands **
    // source registers from scoreboard 
    wire [NUM_SRC_OPDS-1:0][NR_BITS-1:0] src_regs;
    assign src_regs = {rs3, rs2, rs1};

    wire [NUM_SRC_OPDS-1:0] opds_to_fetch;
    
    for (genvar i = 0; i < NUM_SRC_OPDS; ++i) begin : g_opds_to_fetch
        assign opds_to_fetch[i] = (staging_if.data.used_rs[i] && (src_regs[i] != 0));
    end



    // ** SubModule 6 : FSM **
    // control state machine
    always @(*) begin
        state_n = state;
        opds_needed_n = opds_needed;
        opds_busy_n = opds_busy;

        case (state)
        STATE_IDLE: begin
            
            if (staging_if.valid || reduction_service_required) begin
                opds_needed_n = opds_to_fetch;
                opds_busy_n = opds_to_fetch;

                if (opds_to_fetch == 0) begin
                    state_n = STATE_DISPATCH;
                end else begin
                    // Note: reduction_service is guranteed to route here 
                    state_n = STATE_FETCH;
                end
            end

        end

        STATE_FETCH: begin
            if (vgpr_req_fire) begin
                opds_needed_n[vgpr_if.req_data.opd_id] = 0;
            end
            if (vgpr_rsp_fire) begin
                opds_busy_n[vgpr_if.rsp_data.opd_id] = 0;
            end
            if (vopds_busy_n == 0) begin
                state_n = STATE_DISPATCH;
            end

            // Commented out in case future need to read from scalar-rf
            /*
            if (gpr_req_fire) begin
                opds_needed_n[gpr_if.req_data.opd_id] = 0;
            end
            if (gpr_rsp_fire) begin
                opds_busy_n[gpr_if.rsp_data.opd_id] = 0;
            end
            if (opds_busy_n == 0) begin
                state_n = STATE_DISPATCH;
            end
            */
        end

        STATE_DISPATCH: begin
            if (output_ready) begin
                if (simd_eop) begin
                    state_n = STATE_IDLE;
                end else if (opds_to_fetch != 0) begin
                    opds_needed_n = opds_to_fetch;
                    opds_busy_n = opds_to_fetch;
                    state_n = STATE_FETCH;
                end
            end
        end
        endcase
    end

    // ** SubModule 7 : Operands to send requests to gprf (Note: actual sending is by FSM) **
    /*
    wire [SRC_OPD_WIDTH-1:0] opd_id;
    wire opd_fetch_valid;

    VX_priority_encoder #(
        .N (NUM_SRC_OPDS)
    ) opd_id_sel (
        .data_in   (opds_needed),
        .index_out (opd_id),
        .valid_out (opd_fetch_valid),
        `UNUSED_PIN (onehot_out)
    );
    // operands fetch request
    assign gpr_if.req_valid = opd_fetch_valid;
    assign gpr_if.req_data.opd_id = opd_id;
    assign gpr_if.req_data.sid = simd_pid;
    assign gpr_if.req_data.wis = staging_if.data.wis;
    assign gpr_if.req_data.reg_id = src_regs[opd_id];
    */


    // ** SubModule 8 : Operands Fetch Response **
    reg [NUM_SRC_OPDS-1:0][`SIMD_WIDTH-1:0][`XLEN-1:0] opd_values;
    always @(posedge clk) begin
        if (reset || dequeue) begin
            for (integer i = 0; i < NUM_SRC_OPDS; ++i) begin
                opd_values[i] <= '0;
            end
        end else begin
            if (gpr_rsp_fire) begin
                opd_values[gpr_if.rsp_data.opd_id] <= gpr_if.rsp_data.data;
            end
        end
    end

    

    // ** SubModule : state machine update **
    always @(posedge clk) begin
        if (reset) begin
            state <= STATE_IDLE;
            opds_needed <= '0;
            opds_busy <= '0;
        end else begin
            state <= state_n;
            opds_needed <= opds_needed_n;
            opds_busy <= opds_busy_n;
        end
    end




    // TO FIX **************************************************
    // output pending reqs
    assign pending_wis = staging_if.data.wis;
    reg [NUM_REGS-1:0] pending_regs_r;
    always @(*) begin
        pending_regs_r = '0;
        for (integer i = 0; i < NUM_SRC_OPDS; ++i) begin
            if (staging_if.data.used_rs[i]) begin
                pending_regs_r[src_regs[i]] = staging_if.valid;
            end
        end
    end
    assign pending_regs = pending_regs_r;

    // WAR dependency check
    reg [NUM_REGS-1:0] other_pending_regs;
    always @(*) begin
        other_pending_regs = '0;
        for (integer i = 0; i < `NUM_OPCS-1; ++i) begin
            other_pending_regs |= pending_regs_in[i] & {NUM_REGS{staging_if.data.wis == pending_wis_in[i]}};
        end
    end
    wire war_dp_check = staging_if.data.wb && (other_pending_regs[rd] != 0);

    wire output_ready_w;
    assign output_ready = output_ready_w && ~war_dp_check;
    wire output_valid = (state == STATE_DISPATCH) && ~war_dp_check;


    // ** SubModule : NonZero Iterator (skip threads) **
    // simd iterator
    VX_nz_iterator #(
        .DATAW   (`SIMD_WIDTH),
        .N       (SIMD_COUNT),
        .OUT_REG (1)
    ) simd_iter (
        .clk     (clk),
        .reset   (reset),
        .valid_in(staging_if.valid),
        .data_in (staging_if.data.tmask),
        .next    (dequeue),
        `UNUSED_PIN (valid_out),
        .data_out(simd_out),
        .pid     (simd_pid),
        .sop     (simd_sop),
        .eop     (simd_eop)
    );


    // ** SubModule : Send to Dispatch **
    // instruction dispatch
    VX_elastic_buffer #(
        .DATAW   (OUT_DATAW),
        .SIZE    (0),
        .OUT_REG (0)
    ) out_buf (
        .clk      (clk),
        .reset    (reset),
        .valid_in (output_valid),
        .data_in  ({
            staging_if.data.uuid,
            staging_if.data.wis,
            simd_pid,
            simd_out,
            staging_if.data.PC,
            staging_if.data.ex_type,
            staging_if.data.op_type,
            staging_if.data.op_args,
            staging_if.data.wb,
            rd,
            opd_values[0],
            opd_values[1],
            opd_values[2],
            simd_sop,
            simd_eop
        }),
        .ready_in (output_ready_w),
        .valid_out(operands_if.valid),
        .data_out (operands_if.data),
        .ready_out(operands_if.ready)
    );



    // NOT YET FIX *******************
    `ifdef DBG_TRACE_PIPELINE
    always @(posedge clk) begin
        if (scoreboard_if.valid && scoreboard_if.ready) begin
            `TRACE(1, ("%t: %s-input: wid=%0d, PC=0x%0h, ex=", $time, INSTANCE_ID, wis_to_wid(scoreboard_if.data.wis, ISSUE_ID), {scoreboard_if.data.PC, 1'b0}))
            trace_ex_type(1, scoreboard_if.data.ex_type);
            `TRACE(1, (", op="))
            trace_ex_op(1, scoreboard_if.data.ex_type, scoreboard_if.data.op_type, scoreboard_if.data.op_args);
            `TRACE(1, (", tmask=%b, wb=%b, used_rs=%b, rd=%0d, rs1=%0d, rs2=%0d, rs3=%0d (#%0d)\n", scoreboard_if.data.tmask, scoreboard_if.data.wb, scoreboard_if.data.used_rs, to_reg_number(scoreboard_if.data.rd), to_reg_number(scoreboard_if.data.rs1), to_reg_number(scoreboard_if.data.rs2), to_reg_number(scoreboard_if.data.rs3), scoreboard_if.data.uuid))
        end
        if (gpr_if.req_valid && gpr_if.req_ready) begin
            `TRACE(1, ("%t: %s-gpr-req: opd=%0d, wis=%0d, sid=%0d, reg=%0d\n", $time, INSTANCE_ID, gpr_if.req_data.opd_id, wis_to_wid(gpr_if.req_data.wis, ISSUE_ID), gpr_if.req_data.sid, gpr_if.req_data.reg_id))
        end
        if (gpr_if.rsp_valid) begin
            `TRACE(1, ("%t: %s-gpr-rsp: opd=%0d, data=", $time, INSTANCE_ID, gpr_if.rsp_data.opd_id))
            `TRACE_ARRAY1D(1, "0x%0h", gpr_if.rsp_data.data, `SIMD_WIDTH)
            `TRACE(1, ("\n"))
        end
        if (operands_if.valid && operands_if.ready) begin
            `TRACE(1, ("%t: %s-output: wid=%0d, sid=%0d, PC=0x%0h, ex=", $time, INSTANCE_ID, wis_to_wid(operands_if.data.wis, ISSUE_ID), operands_if.data.sid, {operands_if.data.PC, 1'b0}))
            trace_ex_type(1, operands_if.data.ex_type);
            `TRACE(1, (", op="))
            trace_ex_op(1, operands_if.data.ex_type, operands_if.data.op_type, operands_if.data.op_args);
            `TRACE(1, (", tmask=%b, wb=%b, rd=%0d, rs1_data=", operands_if.data.tmask, operands_if.data.wb, operands_if.data.rd))
            `TRACE_ARRAY1D(1, "0x%0h", operands_if.data.rs1_data, `SIMD_WIDTH)
            `TRACE(1, (", rs2_data="))
            `TRACE_ARRAY1D(1, "0x%0h", operands_if.data.rs2_data, `SIMD_WIDTH)
            `TRACE(1, (", rs3_data="))
            `TRACE_ARRAY1D(1, "0x%0h", operands_if.data.rs3_data, `SIMD_WIDTH)
            `TRACE(1, (", "))
            trace_op_args(1, operands_if.data.ex_type, operands_if.data.op_type, operands_if.data.op_args);
            `TRACE(1, (", sop=%b, eop=%b (#%0d)\n", operands_if.data.sop, operands_if.data.eop, operands_if.data.uuid))
        end
    end
`endif

endmodule

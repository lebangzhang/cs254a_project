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

module VX_vopc_unit import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = "",
    parameter ISSUE_ID = 0
) (
    input wire              clk,
    input wire              reset,

    input reg [`NUM_OPCS-1:0] wait_mask,

    // Scoreboard Interface
    VX_scoreboard_if.slave  scoreboard_if,

    // Writeback Interface
    VX_writeback_if.slave   writeback_if,

    /*
    TO FIX
    VX_writeback_if.slave   writeback_in_if,

    VX_writeback_if.master   writeback_out_if, // -> To vgpr writeback interface
    */
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
    localparam OUT_DATAW = UUID_WIDTH + ISSUE_WIS_W + SIMD_IDX_W + VL_WIDTH + `SIMD_WIDTH + PC_BITS + EX_BITS + INST_OP_BITS + INST_ARGS_BITS + 1 + NR_BITS + (NUM_SRC_OPDS * `SIMD_WIDTH * `XLEN) + 1 + 1;

    localparam STATE_IDLE     = 0;
    localparam STATE_FETCH    = 1;
    localparam STATE_DISPATCH = 2;
    localparam STATE_WIDTH    = 2;

    VX_scoreboard_if staging_if();

    wire output_ready;

    wire [`SIMD_WIDTH-1:0] simd_out;
    wire [SIMD_IDX_W-1:0] simd_pid;
    wire simd_sop, simd_eop;

    // Just a pipeline buffer to hold outputs from SB
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

    // Calculate Register Numbers
    wire [NR_BITS-1:0] stg_rd  = to_reg_number(staging_if.data.rd);
    wire [NR_BITS-1:0] stg_rs1 = to_reg_number(staging_if.data.rs1);
    wire [NR_BITS-1:0] stg_rs2 = to_reg_number(staging_if.data.rs2);
    wire [NR_BITS-1:0] stg_rs3 = to_reg_number(staging_if.data.rs3);
    wire [NUM_SRC_OPDS-1:0][NR_BITS-1:0] stg_src_regs = {stg_rs3, stg_rs2, stg_rs1};
    wire [NUM_SRC_OPDS-1:0] stg_rs_is_vec = {staging_if.data.rs3.rtype == 2, staging_if.data.rs2.rtype == 2, staging_if.data.rs1.rtype == 2};
    //wire                    stg_rd_is_vec = {staging_if.data.rd.rtype == 2};

    // Determine source operands to fetch
    wire [NUM_SRC_OPDS-1:0] opds_to_fetch;
    for (genvar i = 0; i < NUM_SRC_OPDS; ++i) begin : g_opds_to_fetch
        assign opds_to_fetch[i] = staging_if.data.used_rs[i] && (stg_src_regs[i] != 0) ;
    end

    /*
    wire is_reduction_instruction = staging_if.data.op_arg.vpu.is_reduction;
    */

    /*
    // For reduction opc
    always@(*)
    if(staging_if.valid && staging_if.data.op_arg.vpu.is_reduction) begin
        then look at writeback ==> check if valid
        check if staging PC ==> matches current instruction
    end
    */

    // ** SubModule 2 : Handle Writeback Interface **
    /*
    // Check if is reduction
    // NOTE: Need a better way of determining is_reduction_signal (if
    // permutation is also considered)

    // Just check for program counter <---- *****
    wire wb_datatype = writeback_if.data.rd[NR_BITS      - 1 : RV_REGS_BITS];
    wire wb_rd_id    = writeback_if.data.rd[RV_REGS_BITS - 1 : 0];

    // Rename: is_wb_reduction = (wb.data.PC == reduce_pc)
    wire is_reduction_signal = writeback_if.valid && _______;
    */


    // ** SubModule 3 : Request and Response fire signals **
    // GP Reg File: Fire Request + Fire Response
    wire gpr_req_fire = gpr_if.req_valid && gpr_if.req_ready;
    wire gpr_rsp_fire = gpr_if.rsp_valid;

    // Vec Reg File: Fire Request + Fire Response
    wire vgpr_req_fire = vgpr_if.req_valid && vgpr_if.req_ready;
    wire vgpr_rsp_fire = vgpr_if.rsp_valid;



    // ** SubModule 4 : Dequeue Signals **

    // dequeue       : True if a request is sent out to dispatch ==> synchronizes both state machines
    // next_simd     : Special case of dequeue when last lane (for gpr)
    // last_dispatch : Special case of next_simd when eop
    wire dequeue       = (state == STATE_DISPATCH) && output_ready;
    wire next_simd     = dequeue && (lane_counter == VL_BITS'(VL_COUNT));
    wire last_dispatch = next_simd && (simd_eop);


    // Pop from staging buff
    assign staging_if.ready = last_dispatch;

    // ** SubModule 5 : Writeback Reduction Servicing **
    /*
    // ** TO FIX UP
    // Reduction info
    reg [31:0][NUM_SRC_OPDS - 1 : 0] reduction_counter;
    reg [31:0][NUM_SRC_OPDS - 1 : 0] reduction_src;

    always @(posedge clk) begin
        if(reset) begin
                reduction_counter[i]  <= '0;
                reduction_src[i]      <= '0;
            end
        end
    end
    */

    // ** SubModule 8 : FSM for gprf **
    // Use centralized state machine for both gpr and vrf
    reg [STATE_WIDTH-1:0]  state, state_n;
    reg [NUM_SRC_OPDS-1:0] gp_opds_needed, gp_opds_needed_n;
    reg [NUM_SRC_OPDS-1:0] v_opds_needed, v_opds_needed_n;
    reg [NUM_SRC_OPDS-1:0] gp_opds_busy, gp_opds_busy_n;
    reg [NUM_SRC_OPDS-1:0] v_opds_busy, v_opds_busy_n;

    reg [VL_WIDTH-1:0] lane_counter, lane_counter_n;

    // TO FIX: NEED TO KNOW ACTUAL SIZE
    // reg ext_counter, ext_counter_n;

    always @(*) begin
        state_n = state;
        gp_opds_needed_n = gp_opds_needed;
        gp_opds_busy_n = gp_opds_busy;
        v_opds_needed_n = v_opds_needed;
        v_opds_busy_n = v_opds_busy;

        lane_counter_n = lane_counter; // Possible Bug 

        case (state)

        STATE_IDLE: begin
            if (staging_if.valid) begin
                gp_opds_needed_n = opds_to_fetch & ~stg_rs_is_vec;
                gp_opds_busy_n = opds_to_fetch & ~stg_rs_is_vec;
                v_opds_needed_n = opds_to_fetch & stg_rs_is_vec;
                v_opds_busy_n = opds_to_fetch & stg_rs_is_vec;

                lane_counter_n = '0;

                if (opds_to_fetch == 0) begin
                    state_n = STATE_DISPATCH;
                end else begin
                    state_n = STATE_FETCH;
                end
            end
        end

        STATE_FETCH: begin
            if (gpr_req_fire) begin
                gp_opds_needed_n[gpr_if.req_data.opd_id] = 0;
            end
            if (gpr_rsp_fire) begin
                gp_opds_busy_n[gpr_if.rsp_data.opd_id] = 0;
            end

            if (vgpr_req_fire) begin
                v_opds_needed_n[vgpr_if.req_data.opd_id] = 0;
            end
            if (vgpr_rsp_fire) begin
                v_opds_busy_n[vgpr_if.rsp_data.opd_id] = 0;
            end

            if (gp_opds_busy_n == 0 && v_opds_busy_n == 0) begin
                state_n = STATE_DISPATCH;
            end
        end

        STATE_DISPATCH: begin
           if (output_ready) begin
                // Last Packet + Last Lane
                if (last_dispatch) begin
                    state_n = STATE_IDLE;

                end else if (dequeue) begin

                    // Get Next Lane
                    if (lane_counter == VL_BITS'(VL_COUNT)) begin
                        lane_counter_n = '0;

                        // Reset general purpose regs
                        if(gp_opds_to_fetch != 0) begin 
                            gp_opds_needed_n = opds_to_fetch & ~stg_rs_is_vec;
                            gp_opds_busy_n = opds_to_fetch & ~stg_rs_is_vec;
                            state_n = STATE_FETCH;
                        end 


                    end else begin
                        lane_counter_n = lane_counter + 1;
                    end

                    if (opds_to_fetch != 0) begin
                        // only reset vector operands
                        v_opds_needed_n = opds_to_fetch & stg_rs_is_vec;
                        v_opds_busy_n = opds_to_fetch & stg_rs_is_vec;
                        state_n = STATE_FETCH;
                    end
                end

            end
        end
        endcase
    end

    // ** SubModule 10 : Control to gprf **

    wire [SRC_OPD_WIDTH-1:0] gp_opd_id;
    wire gp_opd_fetch_valid;

    VX_priority_encoder #(
        .N (NUM_SRC_OPDS)
    ) opd_id_sel (
        .data_in   (gp_opds_needed),
        .index_out (gp_opd_id),
        .valid_out (gp_opd_fetch_valid),
        `UNUSED_PIN (onehot_out)
    );

    // operands fetch request
    assign gpr_if.req_valid = gp_opd_fetch_valid;
    assign gpr_if.req_data.opd_id = gp_opd_id;

    assign gpr_if.req_data.sid = simd_pid;
    assign gpr_if.req_data.wis = staging_if.data.wis;

    assign gpr_if.req_data.reg_id = NR_S_BITS'(stg_src_regs[gp_opd_id]);


    // ** SubModule 11 : Operand Fetch Response from vgpr **
    wire [SRC_OPD_WIDTH-1:0] v_opd_id;
    wire v_opd_fetch_valid;

    VX_priority_encoder #(
        .N (NUM_SRC_OPDS)
    ) v_opd_id_sel (
        .data_in   (v_opds_needed),
        .index_out (v_opd_id),
        .valid_out (v_opd_fetch_valid),
        `UNUSED_PIN (onehot_out)
    );

    // operands fetch request
    assign vgpr_if.req_valid = v_opd_fetch_valid;
    assign vgpr_if.req_data.opd_id = v_opd_id;

    assign vgpr_if.req_data.sid = simd_pid;
    assign vgpr_if.req_data.wis = staging_if.data.wis;

    assign vgpr_if.req_data.lid = lane_counter;
    assign vgpr_if.req_data.reg_id = NR_V_BITS'(stg_src_regs[v_opd_id]);

    // For the following sections
    reg [NUM_SRC_OPDS-1:0][`SIMD_WIDTH-1:0][`XLEN-1:0] opd_values;

    // operands fetch response
    always @(posedge clk) begin

        // Reset only last dispatch
        if (reset || last_dispatch) begin
            for (integer i = 0; i < NUM_SRC_OPDS; ++i) begin
                opd_values[i] <= '0;
            end
        end else begin
            if (gpr_rsp_fire) begin
                opd_values[gpr_if.rsp_data.opd_id] <= gpr_if.rsp_data.data;
            end
            if (vgpr_rsp_fire) begin
                opd_values[vgpr_if.rsp_data.opd_id] <= vgpr_if.rsp_data.data;
            end
        end
    end

    /*
    // Accumulates Partial Writes from WB interface (for reduction)
    for (genvar i = 0; i < SIMD_WIDTH; i++) begin

        if(wb_if.tmask[simd_id * SIMD_WIDTH + i] == 1) begin
           temp_data[i] = wb_if.data.data[simd_id * SIMD_WIDTH + i];
           temp_simd[i] = 1;
        end
    end

    always@(*) begin
        if( 1 == &temp_simd ) begin
            // Got all temp data
            // Start issue next addition
        end
    end
    */

    // ** SubModule 12 : state machine update **
    // ******************
    always @(posedge clk) begin
        if (reset) begin
            state <= STATE_IDLE;

            gp_opds_needed <= '0;
            gp_opds_busy <= '0;

            v_opds_needed <= '0;
            v_opds_busy <= '0;

            lane_counter <= '0;
        end else begin
            state <= state_n;

            gp_opds_needed <= gp_opds_needed_n;
            gp_opds_busy <= gp_opds_busy_n;

            v_opds_needed <= v_opds_needed_n;
            v_opds_busy <= v_opds_busy_n;

            lane_counter <= lane_counter_n;
        end
    end

    // wait for dependency check
    wire dep_check_ready = (wait_mask == 0);

    /*****************************************************************/
    // Set Ready to dispatch signal
    wire output_ready_w;
    assign output_ready = output_ready_w && ~dep_check_ready;
    wire output_valid = (state == STATE_DISPATCH) && ~dep_check_ready;

    // ** SubModule : NonZero Iterator (skip threads) **
    // simd iterator
    // NOT SURE ABOUT THIS *****************
    VX_nz_iterator #(
        .DATAW   (`SIMD_WIDTH),
        .N       (SIMD_COUNT),
        .OUT_REG (1)
    ) simd_iter (
        .clk     (clk),
        .reset   (reset),
        .valid_in(staging_if.valid),
        .data_in (staging_if.data.tmask),
        .next    (next_simd),
        `UNUSED_PIN (valid_out),
        .data_out(simd_out),
        .pid     (simd_pid),
        .sop     (simd_sop),
        .eop     (simd_eop)
    );

    // ** SubModule : Send to Dispatch **
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
            lane_counter,
            staging_if.data.wis,
            simd_pid,
            simd_out,
            staging_if.data.PC,
            staging_if.data.ex_type,
            staging_if.data.op_type,
            staging_if.data.op_args,
            staging_if.data.wb,
            stg_rd, // TODO
            opd_values[0],
            opd_values[1],
            opd_values[2],
            simd_sop, // TODO
            last_dispatch
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

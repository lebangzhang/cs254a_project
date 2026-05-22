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

module VX_uop_sequencer import
`ifdef EXT_V_ENABLE
    VX_vpu_pkg::*,
`endif
`ifdef EXT_TCU_ENABLE
    VX_tcu_pkg::*,
`endif
    VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = "",
    parameter WARP_ID = 0
) (
    input clk,
    input reset,

`ifdef EXT_V_ENABLE
    VX_vpu_seq_csr_if.slave  vpu_seq_csr_if,
    VX_vpu_seq_opc_if.slave  vpu_seq_opc_if,
`endif

    VX_ibuffer_if.slave  input_if,
    VX_ibuffer_if.master output_if
);
    `UNUSED_PARAM (WARP_ID)
    `UNUSED_SPARAM (INSTANCE_ID)

    localparam UOP_SEL_W = `LOG2UP(UOP_MAX);

    // UOP-expanders signals.
    wire [UOP_MAX-1:0]   uop_in_valid;
    ibuffer_t            uop_out_data [UOP_MAX];
    wire [UOP_CTR_W-1:0] uop_out_count [UOP_MAX];

    reg [UOP_CTR_W-1:0] uop_ctr;    // current uop index within active burst
    reg                 uop_active; // high while emitting a uop burst
    reg                 uop_done;   // high when current uop is the last in the burst

    reg [UOP_SEL_W-1:0] sel_idx_r;
    reg [UOP_CTR_W-1:0] last_ctr_r;
    ibuffer_t           uop_data;

`ifdef EXT_V_ENABLE
    reg vpu_seq_pending;

    wire output_is_vset = output_if.data.is_rvv
                       && (output_if.data.ex_type == EX_SFU)
                       && (output_if.data.op_type == INST_OP_BITS'(INST_SFU_VSET));

    wire vpu_seq_wait = vpu_seq_pending && ~vpu_seq_opc_if.valid;
`endif

    logic [UOP_SEL_W-1:0] sel_idx_n;
    wire is_uop_input;
    VX_priority_encoder #(
        .N (UOP_MAX),
        .REVERSE (1)
    ) priority_enc (
        .data_in    (uop_in_valid),
        `UNUSED_PIN (onehot_out),
        .index_out  (sel_idx_n),
        .valid_out  (is_uop_input)
    );

    // prepare the input data for the uop expanders
    ibuffer_t uop_in_data;
    always_comb begin
        uop_in_data = input_if.data;
        uop_in_data.uuid = get_uop_uuid(input_if.data.uuid, uop_ctr);
    end

    // uop_start fires for exactly one cycle at the beginning of a new burst.
    wire uop_start = input_if.valid && is_uop_input && ~uop_active
                  `ifdef EXT_V_ENABLE
                   && ~vpu_seq_wait
                  `endif
                   ;

    // downstream accepted a uop this cycle.
    wire uop_next = uop_active && output_if.ready;

    // Sequential state machine: track the active burst and uop index.
    always_ff @(posedge clk) begin
        if (reset) begin
            uop_ctr    <= '0;
            sel_idx_r  <= '0;
            last_ctr_r <= '0;
            uop_active <= 1'b0;
            uop_done   <= 1'b0;
        `ifdef EXT_V_ENABLE
            vpu_seq_pending <= 1'b0;
        `endif
        end else begin
        `ifdef EXT_V_ENABLE
            if (vpu_seq_opc_if.valid) begin
                vpu_seq_pending <= 1'b0;
            end
            if (output_if.valid && output_if.ready && output_is_vset) begin
                vpu_seq_pending <= 1'b1;
            end
        `endif
            if (uop_start) begin
                uop_active <= 1'b1;
                uop_ctr    <= UOP_CTR_W'(1);
                sel_idx_r  <= sel_idx_n;
                last_ctr_r <= uop_out_count[sel_idx_n] - UOP_CTR_W'(1);
                uop_data   <= uop_out_data[sel_idx_n];
                uop_done   <= (uop_out_count[sel_idx_n] == UOP_CTR_W'(1));
            end else if (uop_next) begin
                uop_active <= ~uop_done;
                uop_ctr    <= uop_done ? '0 : uop_ctr + UOP_CTR_W'(1);
                uop_data   <= uop_out_data[sel_idx_r];
                uop_done   <= (uop_ctr == last_ctr_r);
            end
        end
    end

    wire [UOP_MAX-1:0] uop_in_start;
    for (genvar i = 0; i < UOP_MAX; ++i) begin : g_start
        assign uop_in_start[i] = uop_start && uop_in_valid[i];
    end

    wire [UOP_MAX-1:0] uop_in_next;
    for (genvar i = 0; i < UOP_MAX; ++i) begin : g_next
        assign uop_in_next[i] = uop_next && uop_in_valid[i];
    end

    // ------------------------------------------------------------------
    // Pack Load/Store uop expander
    // ------------------------------------------------------------------
    assign uop_in_valid[UOP_PACKLD] = (uop_in_data.ex_type == EX_LSU)
                                   && ~uop_in_data.is_rvv
                                   && (uop_in_data.op_args.lsu.pack != 0);
    VX_uop_packld uop_packld (
        .clk       (clk),
        .reset     (reset),
        .ibuf_in   (uop_in_data),
        .start     (uop_in_start[UOP_PACKLD]),
        .advance   (uop_in_next[UOP_PACKLD]),
        .uop_idx   (uop_ctr),
        .ibuf_out  (uop_out_data[UOP_PACKLD]),
        .uop_count (uop_out_count[UOP_PACKLD])
    );

`ifdef EXT_TCU_ENABLE
    // ------------------------------------------------------------------
    // TCU uop expander
    // ------------------------------------------------------------------
    assign uop_in_valid[UOP_TCU] = (uop_in_data.ex_type == EX_TCU)
        && (uop_in_data.op_type == INST_TCU_WMMA
    `ifdef TCU_WGMMA_ENABLE
        || uop_in_data.op_type == INST_TCU_WGMMA
    `endif
        );
    VX_tcu_uops tcu_uops (
        .clk       (clk),
        .reset     (reset),
        .ibuf_in   (uop_in_data),
        .start     (uop_in_start[UOP_TCU]),
        .advance   (uop_in_next[UOP_TCU]),
        .uop_idx   (uop_ctr),
        .ibuf_out  (uop_out_data[UOP_TCU]),
        .uop_count (uop_out_count[UOP_TCU])
    );
`endif

`ifdef EXT_V_ENABLE
    // ------------------------------------------------------------------
    // VPU uop expander
    // ------------------------------------------------------------------
    assign uop_in_valid[UOP_VPU] = uop_in_data.is_rvv;
    VX_vpu_uops vpu_uops (
        .clk            (clk),
        .reset          (reset),
        .vpu_seq_csr_if (vpu_seq_csr_if),
        .vpu_seq_opc_if (vpu_seq_opc_if),
        .ibuf_in        (uop_in_data),
        .start          (uop_in_start[UOP_VPU]),
        .advance        (uop_in_next[UOP_VPU]),
        .uop_idx        (uop_ctr),
        .ibuf_out       (uop_out_data[UOP_VPU]),
        .uop_count      (uop_out_count[UOP_VPU])
    );
`endif

    wire uop_hold = is_uop_input && ~uop_active
                 `ifdef EXT_V_ENABLE
                  && ~vpu_seq_wait
                 `endif
                  ;

    assign output_if.valid = uop_active || (input_if.valid && ~uop_hold
                         `ifdef EXT_V_ENABLE
                          && ~vpu_seq_wait
                         `endif
                          );
    assign output_if.data  = uop_active ? uop_data : input_if.data;
    assign input_if.ready  = output_if.ready && (uop_active ? uop_done : (~uop_hold
                         `ifdef EXT_V_ENABLE
                          && ~vpu_seq_wait
                         `endif
                          ));

`ifdef DBG_TRACE_PIPELINE
    always @(posedge clk) begin
        if (output_if.valid && output_if.ready && uop_active) begin
            `TRACE(1, ("%t: %s decode: wid=%0d, PC=0x%0h, ex=", $time, INSTANCE_ID, WARP_ID, to_fullPC(output_if.data.PC)))
            VX_trace_pkg::trace_ex_type(1, output_if.data.ex_type);
            `TRACE(1, (", op="))
            VX_trace_pkg::trace_ex_op(1, output_if.data.ex_type, output_if.data.op_type, output_if.data.op_args);
            `TRACE(1, (", tmask=%b, wb=%b, rd_xregs=%b, wr_xregs=%b, used_rs=%b, rd=", output_if.data.tmask, output_if.data.wb, output_if.data.rd_xregs, output_if.data.wr_xregs, output_if.data.used_rs))
            VX_trace_pkg::trace_reg_idx(1, output_if.data.rd);
            `TRACE(1, (", rs1="))
            VX_trace_pkg::trace_reg_idx(1, output_if.data.rs1);
            `TRACE(1, (", rs2="))
            VX_trace_pkg::trace_reg_idx(1, output_if.data.rs2);
            `TRACE(1, (", rs3="))
            VX_trace_pkg::trace_reg_idx(1, output_if.data.rs3);
            VX_trace_pkg::trace_op_args(1, output_if.data.ex_type, output_if.data.op_type, output_if.data.op_args);
            `TRACE(1, (", parent=#%0d", input_if.data.uuid))
            `TRACE(1, (" (#%0d)\n", output_if.data.uuid))
        end
    end
`endif

endmodule

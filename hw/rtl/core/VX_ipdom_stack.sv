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

module VX_ipdom_stack import VX_gpu_pkg::*; #(
    parameter WIDTH   = 1,
    parameter DEPTH   = 1,
    parameter ADDRW   = `LOG2UP(DEPTH),
    parameter OUT_REG = 0
) (
    input  wire             clk,
    input  wire             reset,
    input wire [NW_WIDTH-1:0] wid,
    input  wire [WIDTH-1:0] q0,
    input  wire [WIDTH-1:0] q1,
    output wire [WIDTH-1:0] d,
    output wire             d_set,
    output wire [`NUM_WARPS-1:0][ADDRW-1:0] q_ptr,
    input  wire             push,
    input  wire             pop,
    output wire             empty,
    output wire             full
);
    localparam BRAM_DATAW = 1 + WIDTH * 2;
    localparam BRAM_SIZE  = DEPTH * `NUM_WARPS;
    localparam BRAW_ADDRW = `LOG2UP(BRAM_SIZE);

    wire [`NUM_WARPS-1:0][ADDRW-1:0] rd_ptr_w, wr_ptr_w;
    wire [`NUM_WARPS-1:0] empty_w, full_w;
    wire d_set_r;

    wire pop_r;
    wire [NW_WIDTH-1:0] wid_r;
    `BUFFER_EX({pop_r, wid_r}, {pop, wid}, 1'b1, 1, OUT_REG);

    for (genvar i = 0; i < `NUM_WARPS; i++) begin : g_addressing

        reg [ADDRW-1:0] rd_ptr_r, wr_ptr_r;
        reg empty_r, full_r;

        wire push_s = push && (wid == i);
        wire pop_s = pop_r && (wid_r == i);

        `RUNTIME_ASSERT(~(push_s && full_r), ("%t: runtime error: writing to a full stack!", $time));
        `RUNTIME_ASSERT(~(pop_s && empty_r), ("%t: runtime error: reading an empty stack!", $time));
        `RUNTIME_ASSERT(~(push_s && pop_s),  ("%t: runtime error: push and pop in same cycle not supported!", $time));

        always @(posedge clk) begin
            if (reset) begin
                rd_ptr_r <= '0;
                wr_ptr_r <= '0;
                empty_r  <= 1;
                full_r   <= 0;
            end else begin
                if (push_s) begin
                    rd_ptr_r <= wr_ptr_r;
                    wr_ptr_r <= wr_ptr_r + ADDRW'(1);
                    empty_r  <= 0;
                    full_r   <= (ADDRW'(DEPTH-1) == wr_ptr_r);
                end else if (pop_s) begin
                    rd_ptr_r <= rd_ptr_r - ADDRW'(d_set_r);
                    wr_ptr_r <= wr_ptr_r - ADDRW'(d_set_r);
                    empty_r  <= (rd_ptr_r == 0) && d_set_r;
                    full_r   <= 0;
                end
            end
        end

        assign rd_ptr_w[i] = rd_ptr_r;
        assign wr_ptr_w[i] = wr_ptr_r;
        assign empty_w[i]  = empty_r;
        assign full_w[i]   = full_r;
    end

    wire [WIDTH-1:0] d0_r, d1_r;
    wire [BRAW_ADDRW-1:0] raddr, waddr;

    if (`NUM_WARPS > 1) begin : g_wN
        assign waddr = push ? {wr_ptr_w[wid], wid} : {rd_ptr_w[wid_r], wid_r};
        assign raddr = {rd_ptr_w[wid], wid};
    end else begin : g_w0
        assign waddr = push ? wr_ptr_w : rd_ptr_w;
        assign raddr = rd_ptr_w;
    end

    VX_dp_ram #(
        .DATAW    (BRAM_DATAW),
        .SIZE     (BRAM_SIZE),
        .OUT_REG  (OUT_REG),
        .RDW_MODE ("R")
    ) ipdom_store (
        .clk   (clk),
        .reset (reset),
        .read  (pop),
        .write (push || pop_r),
        .wren  (1'b1),
        .waddr (waddr),
        .raddr (raddr),
        .wdata (push ? {1'b0, q1, q0} : {1'b1, d1_r, d0_r}),
        .rdata ({d_set_r, d1_r, d0_r})
    );

    assign d     = d_set_r ? d0_r : d1_r;
    assign d_set = ~d_set_r;
    assign q_ptr = wr_ptr_w;
    assign empty = empty_w[wid];
    assign full  = full_w[wid];

endmodule

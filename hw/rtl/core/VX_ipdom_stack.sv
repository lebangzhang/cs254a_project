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

`include "VX_platform.vh"

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
    wire [`NUM_WARPS-1:0][ADDRW-1:0] rd_ptr, rd_ptr_n, wr_ptr;
    wire [`NUM_WARPS-1:0]empty_s, full_s;
    wire d_set_w;

    wire pop_d;
    wire [NW_WIDTH-1:0] wid_d;

    `BUFFER_EX(pop_d, pop, 1'b1, 1, OUT_REG);
    `BUFFER_EX(wid_d, wid, 1'b1, 1, OUT_REG);

    for (genvar i = 0; i < `NUM_WARPS; i++) begin : g_addressing

        reg [ADDRW-1:0] rd_ptr_w, rd_ptr_n_w, wr_ptr_w;
        reg empty_r_w, full_r_w;

        wire pop_w = pop_d && (wid_d == i);
        wire push_w = push && (wid == i);

        always @(*) begin
            rd_ptr_n_w = rd_ptr_w;
            if (push_w) begin
                rd_ptr_n_w = wr_ptr_w;
            end else if (pop_w) begin
                rd_ptr_n_w = rd_ptr_w - ADDRW'(d_set_w);
            end
        end

        always @(posedge clk) begin
            if (reset) begin
                wr_ptr_w  <= '0;
                empty_r_w <= 1;
                full_r_w  <= 0;
                rd_ptr_w  <= '0;
            end else begin
                if (push_w) begin
                    wr_ptr_w  <= wr_ptr_w + ADDRW'(1);
                    empty_r_w <= 0;
                    full_r_w  <= (ADDRW'(DEPTH-1) == wr_ptr_w);
                end else if (pop_w) begin
                    wr_ptr_w  <= wr_ptr_w - ADDRW'(d_set_w);
                    empty_r_w <= (rd_ptr_w == 0) && d_set_w;
                    full_r_w  <= 0;
                end
                rd_ptr_w <= rd_ptr_n_w;
            end
        end

        assign rd_ptr[i]   = rd_ptr_w;
        assign rd_ptr_n[i] = rd_ptr_n_w;
        assign wr_ptr[i]   = wr_ptr_w;
        assign empty_s[i]  = empty_r_w;
        assign full_s[i]   = full_r_w;
    end

    `RUNTIME_ASSERT(~push || ~full, ("%t: runtime error: writing to a full stack!", $time));
    `RUNTIME_ASSERT(~pop || ~empty, ("%t: runtime error: reading an empty stack!", $time));
    `RUNTIME_ASSERT(~push || ~pop,  ("%t: runtime error: push and pop in same cycle not supported!", $time));

    wire [WIDTH-1:0] d0, d1;

    wire [WIDTH * 2:0] wdata = push ? {1'b0, q1, q0} : {1'b1, d1, d0};

    VX_dp_ram #(
        .DATAW (1 + WIDTH * 2),
        .SIZE (DEPTH * `NUM_WARPS),
        .OUT_REG (OUT_REG),
        .RDW_MODE ("R")
    ) ipdom_store (
        .clk   (clk),
        .reset (reset),
        .read  (pop),
        .write (push || pop_d),
        .wren  (1'b1),
        .waddr (push ? {wid, wr_ptr[wid]} : {wid_d, rd_ptr[wid_d]}),
        .raddr ({wid, rd_ptr_n[wid]}),
        .wdata (wdata),
        .rdata ({d_set_w, d1, d0})
    );

    assign d     = d_set_w ? d0 : d1;
    assign d_set = ~d_set_w;
    assign q_ptr = wr_ptr;
    assign empty = empty_s[wid];
    assign full  = full_s[wid];

endmodule

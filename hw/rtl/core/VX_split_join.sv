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

module VX_split_join import VX_gpu_pkg::*; #(
    parameter `STRING INSTANCE_ID = "",
    parameter OUT_REG = 0
) (
    input  wire                     clk,
    input  wire                     reset,
    input  wire                     valid,
    input  wire [NW_WIDTH-1:0]      wid,
    input  split_t                  split,
    input  join_t                   sjoin,
    output wire                     join_valid,
    output wire                     join_is_dvg,
    output wire                     join_is_else,
    output wire [NW_WIDTH-1:0]      join_wid,
    output wire [`NUM_THREADS-1:0]  join_tmask,
    output wire [PC_BITS-1:0]       join_pc,
    input  wire [NW_WIDTH-1:0]      stack_wid,
    output wire [DV_STACK_SIZEW-1:0] stack_ptr
);
    `UNUSED_SPARAM (INSTANCE_ID)

    wire [`NUM_WARPS-1:0][DV_STACK_SIZEW-1:0] ipdom_q_ptr;

    wire [(`NUM_THREADS+PC_BITS)-1:0] ipdom_q0 = {split.then_tmask | split.else_tmask, PC_BITS'(0)};
    wire [(`NUM_THREADS+PC_BITS)-1:0] ipdom_q1 = {split.else_tmask, split.next_pc};

    wire ipdom_push = valid && split.valid && split.is_dvg;
    wire ipdom_pop  = valid && sjoin.valid && sjoin.is_dvg;

    VX_ipdom_stack #(
        .WIDTH (`NUM_THREADS + PC_BITS),
        .DEPTH (DV_STACK_SIZE),
        .OUT_REG (OUT_REG)
    ) ipdom_stack (
        .clk   (clk),
        .reset (reset),
        .wid   (wid),
        .q0    (ipdom_q0),
        .q1    (ipdom_q1),
        .d     ({join_tmask, join_pc}),
        .d_set (join_is_else),
        .q_ptr (ipdom_q_ptr),
        .push  (ipdom_push),
        .pop   (ipdom_pop),
        `UNUSED_PIN (empty),
        `UNUSED_PIN (full)
    );

    VX_pipe_register #(
        .DATAW  (1 + NW_WIDTH + 1),
        .RESETW (1),
        .DEPTH  (OUT_REG)
    ) pipe_reg (
        .clk      (clk),
        .reset    (reset),
        .enable   (1'b1),
        .data_in  ({valid && sjoin.valid, wid,      sjoin.is_dvg}),
        .data_out ({join_valid,           join_wid, join_is_dvg})
    );

    assign stack_ptr = ipdom_q_ptr[stack_wid];

endmodule

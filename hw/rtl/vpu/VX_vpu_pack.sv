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

module VX_vpu_pack import VX_gpu_pkg::*, VX_vpu_pkg::*; #(
    parameter NUM_LANES = 1
) (
    input  wire [SEW_TYPE_W-1:0]           sew_type,    // 0→8,1→16,2→32,3→64
    input  wire [SEW_IDX_W-1:0]            sew_idx,     // element index within lane
    input  wire [NUM_LANES-1:0][`XLEN-1:0] data_in,     // per-lane element value
    input  wire [NUM_LANES-1:0]            mask_in,     // per-lane predicate mask bit
    output reg  [NUM_LANES-1:0][`XLEN-1:0] data_out,    // per-lane packed word
    output reg  [NUM_LANES-1:0][XLENB-1:0] mask_out     // per-lane byte mask
);
`ifdef XLEN_64
    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_unpack
        wire [`XLEN-1:0]  data_in8 = `XLEN'(data_in[i][7:0])  << (sew_idx[2:0] * 8);
        wire [`XLEN-1:0] data_in16 = `XLEN'(data_in[i][15:0]) << (sew_idx[1:0] * 16);
        wire [`XLEN-1:0] data_in32 = `XLEN'(data_in[i][31:0]) << (sew_idx[0:0] * 32);
        wire [`XLEN-1:0] data_in64 = data_in[i];
        always @(*) begin
            case (sew_type)
            2'd0: data_out[i] = data_in8;
            2'd1: data_out[i] = data_in16;
            2'd2: data_out[i] = data_in32;
            2'd3: data_out[i] = data_in64;
            endcase
        end
        always @(*) begin
            case (sew_type)
            2'd0: mask_out[i] = XLENB'({1{mask_in[i]}}) << (sew_idx[2:0] * 1);
            2'd1: mask_out[i] = XLENB'({2{mask_in[i]}}) << (sew_idx[1:0] * 2);
            2'd2: mask_out[i] = XLENB'({4{mask_in[i]}}) << (sew_idx[0])  * 4;
            2'd3: mask_out[i] = XLENB'({8{mask_in[i]}});
            endcase
        end
    end
`else
    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_unpack
        wire [`XLEN-1:0]  data_in8 = `XLEN'(data_in[i][7:0])  << (sew_idx[1:0] * 8);
        wire [`XLEN-1:0] data_in16 = `XLEN'(data_in[i][15:0]) << (sew_idx[0:0] * 16);
        wire [`XLEN-1:0] data_in32 = data_in[i];
        always @(*) begin
            case (sew_type)
            2'd0: data_out[i] = data_in8;
            2'd1: data_out[i] = data_in16;
            2'd2: data_out[i] = data_in32;
            2'd3: data_out[i] = 'x;
            endcase
        end
        always @(*) begin
            case (sew_type)
            2'd0: mask_out[i] = XLENB'({1{mask_in[i]}}) << (sew_idx[1:0] * 1);
            2'd1: mask_out[i] = XLENB'({2{mask_in[i]}}) << (sew_idx[0:0] * 2);
            2'd2: mask_out[i] = XLENB'({4{mask_in[i]}});
            2'd3: mask_out[i] = 'x;
            endcase
        end
    end
`endif

endmodule

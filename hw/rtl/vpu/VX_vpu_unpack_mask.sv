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

module VX_vpu_unpack_mask import VX_gpu_pkg::*, VX_vpu_pkg::*; #(
    parameter NUM_LANES = 1
) (
    input  wire [SEW_TYPE_W-1:0]           sew_type,  // 0→8,1→16,2→32,3→64
    input  wire [SEW_IDX_W-1:0]            sew_idx,   // element index within lane
    input  wire [NUM_LANES-1:0][`XLEN-1:0] data_in,   // per-lane packed word
    output reg  [NUM_LANES-1:0]            data_out   // per-lane element value
);
    localparam INPUT_ELEMS = XLENB * NUM_LANES;

    wire [INPUT_ELEMS-1:0] elems_in;
    for (genvar i = 0; i < INPUT_ELEMS; ++i) begin : g_unpack
        assign elems_in[i] = data_in[i / XLENB][i % XLENB];
    end

`ifdef XLEN_64
    wire [NUM_LANES-1:0] data_out8, data_out16, data_out32, data_out64;
    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_unpack8
        assign data_out8[i]  = elems_in[sew_idx[2:0] * NUM_LANES +: NUM_LANES][i];
        assign data_out16[i] = elems_in[sew_idx[1:0] * NUM_LANES +: NUM_LANES][i];
        assign data_out32[i] = elems_in[sew_idx[0] * NUM_LANES +: NUM_LANES][i];
        assign data_out64[i] = elems_in[i];
    end

    always @(*) begin
        case (sew_type)
        2'd0: data_out = data_out8;
        2'd1: data_out = data_out16;
        2'd2: data_out = data_out32;
        2'd3: data_out = data_out64;
        endcase
    end
`else
    wire [NUM_LANES-1:0] data_out8, data_out16, data_out32;
    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_unpack8
        assign data_out8[i]  = elems_in[sew_idx[1:0] * NUM_LANES +: NUM_LANES][i];
        assign data_out16[i] = elems_in[sew_idx[0] * NUM_LANES +: NUM_LANES][i];
        assign data_out32[i] = elems_in[i];
    end

    always @(*) begin
        case (sew_type)
        2'd0: data_out = data_out8;
        2'd1: data_out = data_out16;
        2'd2: data_out = data_out32;
        2'd3: data_out = 'x;
        endcase
    end
`endif

endmodule

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

module VX_vpu_unpack import VX_gpu_pkg::*, VX_vpu_pkg::*; #(
    parameter NUM_LANES = 1
) (
    input  wire [ETW_TYPE_W-1:0]           etw_type,  // 0→8,1→16,2→32,3→64
    input  wire [ETW_IDX_W-1:0]            etw_idx,   // element index within lane
    input  wire                            is_signed, // need to sign-extend
    input  wire [NUM_LANES-1:0][`XLEN-1:0] data_in,   // per-lane packed word
    output wire [NUM_LANES-1:0][`XLEN-1:0] data_out   // per-lane element value
);
`ifdef XLEN_64
    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_unpack
        wire [7:0]  elem8  = data_in[i][etw_idx[2:0] * 8  +: 8];
        wire [15:0] elem16 = data_in[i][etw_idx[1:0] * 16 +: 16];
        wire [31:0] elem32 = data_in[i][etw_idx[0:0] * 32 +: 32];
        wire [63:0] elem64 = data_in[i];
        always @(*) begin
          case (etw_type)
            2'd0: data_out[i] = {{(`XLEN-8){is_signed & elem8[7]}}, elem8};
            2'd1: data_out[i] = {{(`XLEN-16){is_signed & elem16[15]}}, elem16};
            2'd2: data_out[i] = {{(`XLEN-32){is_signed & elem32[31]}}, elem32};
            2'd3: data_out[i] = elem64;
          endcase
        end
    end
`else
    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_unpack
        wire [7:0]  elem8  = data_in[i][etw_idx[1:0] * 8  +: 8];
        wire [15:0] elem16 = data_in[i][etw_idx[0:0] * 16 +: 16];
        wire [31:0] elem32 = data_in[i];
        always @(*) begin
          case (etw_type)
            2'd0: data_out[i] = {{(`XLEN-8){is_signed & elem8[7]}}, elem8};
            2'd1: data_out[i] = {{(`XLEN-16){is_signed & elem16[15]}}, elem16};
            2'd2: data_out[i] = elem32;
            2'd3: data_out[i] = 'x;
          endcase
        end
    end
`endif

endmodule

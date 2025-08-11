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

module VX_vpu_pack_mask import VX_gpu_pkg::*, VX_vpu_pkg::*; #(
    parameter NUM_LANES = 1
) (
    input  wire [SEW_TYPE_W-1:0]           sew_type,    // 0→8,1→16,2→32,3→64
    input  wire [SEW_IDX_W-1:0]            sew_idx,     // element index within lane
    input  wire [NUM_LANES-1:0][`XLEN-1:0] data_in,     // per-lane element value
    input  wire [NUM_LANES-1:0]            mask_in,     // per-lane predicate mask bit
    output wire [NUM_LANES-1:0][`XLEN-1:0] data_out,    // per-lane packed word
    output wire [NUM_LANES-1:0][XLENB-1:0] mask_out     // per-lane byte mask
);
    `UNUSED_VAR (sew_type);
    `UNUSED_VAR (sew_idx);
    `UNUSED_VAR (data_in);
    `UNUSED_VAR (mask_in);

    assign data_out = '0;
    assign mask_out = '0;

endmodule

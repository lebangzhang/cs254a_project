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

module VX_vpu_uops import VX_vpu_pkg::*, VX_gpu_pkg::*; (
    input clk,
    input reset,

`ifdef EXT_V_ENABLE
    VX_vpu_seq_csr_if.master vpu_seq_csr_if,
    VX_vpu_seq_opc_if.slave  vpu_seq_opc_if,
`endif
    input  wire      start,
    input  wire      next,
    input  ibuffer_t ibuf_in,
    output ibuffer_t ibuf_out,
    output reg       done
);
    `UNUSED_VAR (clk)
    `UNUSED_VAR (reset)
    
    `UNUSED_VAR (ibuf_in)
    `UNUSED_VAR (start)
    `UNUSED_VAR (next)

    `UNUSED_VAR (vpu_seq_opc_if.valid)
    `UNUSED_VAR (vpu_seq_opc_if.wis)
    `UNUSED_VAR (vpu_seq_opc_if.data)

    assign vpu_seq_csr_if.data = '0;
    assign ibuf_out = '0;
    assign done = 0;

endmodule

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

`ifndef VX_VPU_PKG_VH
`define VX_VPU_PKG_VH

`include "VX_define.vh"

package VX_vpu_pkg;

    import VX_gpu_pkg::*;

    /*
    localparam VLMAX_SEW08_LMUL1 =  `VLEN / 8;
    localparam VLMAX_SEW16_LMUL1 =  `VLEN / 16;
    localparam VLMAX_SEW32_LMUL1 =  `VLEN / 32;
    localparam VLMAX_SEW64_LMUL1 =  `VLEN / 64;
    localparam VEC_IMM_BITS      =  15;

    function automatic logic [VL_MAX_W-1:0] vlmax_cacl(input logic[2:0] vlmul, input logic[1:0] vsew);
        logic [VL_MAX_W-1:0] vlen_lmul = (vlmul == 3'b000) ? VLENB       : // vlmul = 1
                                         (vlmul == 3'b001) ? VLENB << 1  : // vlmul = 2
                                         (vlmul == 3'b010) ? VLENB << 2  : // vlmul = 4
                                         (vlmul == 3'b011) ? VLENB << 3  : // vlmul = 8
                                         (vlmul == 3'b111) ? VLENB >> 1  : // vlmul = 1/2
                                         (vlmul == 3'b110) ? VLENB >> 2  : // vlmul = 1/4
                                                            VLENB >> 3;   // vlmul = 1/8 (101)
        return (vlen_lmul >> vsew);
    endfunction
    */

endpackage

`endif // VX_VPU_PKG_VH

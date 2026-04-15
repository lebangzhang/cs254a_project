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

    /*localparam INST_VPU_VL =        4'b0000;
    localparam INST_VPU_VLS =       4'b0001;
    localparam INST_VPU_VLX =       4'b0010;

    localparam INST_VPU_VS =        4'b0100;
    localparam INST_VPU_VSS =       4'b0101;
    localparam INST_VPU_VSX =       4'b0110;

    localparam INST_VPU_OPIVV =     4'b1000;
    localparam INST_VPU_OPFVV =     4'b1001;
    localparam INST_VPU_OPMVV =     4'b1010;
    localparam INST_VPU_OPIVI =     4'b1011;
    localparam INST_VPU_OPIVX =     4'b1100;
    localparam INST_VPU_OPFVF =     4'b1101;
    localparam INST_VPU_OPMVX =     4'b1110;

    localparam INST_VPU_VSETVL =    4'b0011;
    localparam INST_VPU_VSETVLI =   4'b0111;
    localparam INST_VPU_VSETIVLI =  4'b1111;

    localparam INST_VPU_VADD =      6'b100000;
    localparam INST_VPU_VSUB =      6'b111111;
    localparam INST_VPU_VMINU =     6'b111110;
    localparam INST_VPU_VMIN =      6'b100001;
    localparam INST_VPU_VMAXU =     6'b100010;
    localparam INST_VPU_VMAX =      6'b100011;
    localparam INST_VPU_VMSEQ =     6'b100100;
    localparam INST_VPU_VMSNE =     6'b100101;
    localparam INST_VPU_VMSLEU =    6'b100110;
    localparam INST_VPU_VMSLE =     6'b100111;
    localparam INST_VPU_VMFNE =     6'b101000;
    localparam INST_VPU_VFMACC =    6'b101001;
    localparam INST_VPU_VREDSUM =   6'b101010;
    localparam INST_VPU_VMV_XS =    6'b101011;
    localparam INST_VPU_VMV_VI =    6'b101100;
    localparam INST_VPU_VMANDNOT =  6'b101101;
    localparam INST_VPU_VMORNOT =   6'b101110;
    localparam INST_VPU_VMNAND =    6'b101111;
    localparam INST_VPU_VMNOR =     6'b110000;
    localparam INST_VPU_VMXNOR =    6'b110001;
    localparam INST_VPU_VMACC =     6'b110010;
    localparam INST_VPU_ADDI =      6'b110011;
    localparam INST_VPU_VMV1R =     6'b110100;
    localparam INST_VPU_VRSUB =     6'b110101;
    localparam INST_VPU_VFMV =      6'b110110;
    localparam INST_VPU_VFMERGE =   6'b110111;
    localparam INST_VPU_VMSGTU =    6'b111000;
    localparam INST_VPU_VMSGT =     6'b111001;
    localparam INST_VPU_VFRSUB =    6'b111010;
    localparam INST_VPU_VSLIDE1UP = 6'b111011;
    localparam INST_VPU_VSLIDE1DOWN=6'b111100;
    localparam INST_VPU_VMV_SX =    6'b111101;

    localparam INST_VPU_OP_BITS = 4;

    localparam INST_VPU_VLD  =      2'b00;
    localparam INST_VPU_VST  =      2'b01;
    localparam INST_VPU_VOP  =      2'b10;
    localparam INST_VPU_VSET =      2'b11;
    localparam INST_VPU_BITS =      2;*/

    typedef struct packed {
        logic [EX_BITS-1:0]      ex_type;
        logic [INST_OP_BITS-1:0] op_type;
        op_args_t                op_args;
        logic                    is_masked;
        logic [NUM_SRC_OPDS:0][NUM_REGS_BITS-1:0] reg_ids;
        logic [NUM_SRC_OPDS:0]   use_regs;
    } vpu_decode_t;

    function automatic logic insn_is_unsigned(input logic [EX_BITS-1:0] ex_type,
                                              input logic [INST_OP_BITS-1:0] op_type);
        `UNUSED_VAR (ex_type)
        `UNUSED_VAR (op_type)
        return 0;
    endfunction

    function automatic logic insn_is_masked(input logic [EX_BITS-1:0] ex_type,
                                            input logic [INST_OP_BITS-1:0] op_type);
        `UNUSED_VAR (ex_type)
        `UNUSED_VAR (op_type)
        return 0;
    endfunction

    function automatic logic [NUM_SREGS_BITS-1:0] to_sreg_number(input logic [NUM_REGS_BITS-1:0] reg_num);
        `UNUSED_VAR (reg_num)
        return NUM_SREGS_BITS'(reg_num);
    endfunction

    function automatic logic [NUM_VREGS_BITS-1:0] to_vreg_number(input logic [NUM_REGS_BITS-1:0] reg_num);
        `UNUSED_VAR (reg_num)
        return NUM_VREGS_BITS'(reg_num);
    endfunction

    // Compute vlmax per RVV spec.
    //   vlmul[2] (MSB) distinguishes fractional (1) vs integer (0) LMUL.
    //   fractional: vlen_mul = VLENB >> (8 - vlmul)   // vlmul in {5,6,7} -> LMUL 1/8,1/4,1/2
    //   integer:    vlen_mul = VLENB << vlmul         // vlmul in {0..3}  -> LMUL 1,2,4,8
    //   vlmax = vlen_mul >> vsew
    // Mirrors simx/vec_unit.cpp configure().
    localparam VLMAX_CALC_W = VL_MAX_W + 4;
    function automatic logic [VL_MAX_W-1:0] vlmax_cacl(
        input logic [2:0] vlmul,
        input logic [2:0] vsew
    );
        logic [VLMAX_CALC_W-1:0] vlenb_w;
        logic [VLMAX_CALC_W-1:0] vlen_mul;
        vlenb_w = VLMAX_CALC_W'(VLENB);
        if (vlmul[2]) begin
            vlen_mul = vlenb_w >> (4'd8 - {1'b0, vlmul});
        end else begin
            vlen_mul = vlenb_w << vlmul;
        end
        return VL_MAX_W'(vlen_mul >> vsew);
    endfunction

    // vill = (SEW > XLEN) || (vlmax > VLEN)
    // SEW bytes = (1 << vsew); compare to XLENB.
    function automatic logic vill_calc(
        input logic [2:0]          vsew,
        input logic [VL_MAX_W-1:0] vlmax
    );
        logic sew_exceeds;
        logic vlmax_exceeds;
        sew_exceeds   = ((32'd1 << vsew) > 32'(XLENB));
        vlmax_exceeds = ({1'b0, vlmax} > (VL_MAX_W+1)'(`VLEN));
        return sew_exceeds | vlmax_exceeds;
    endfunction

endpackage

`endif // VX_VPU_PKG_VH

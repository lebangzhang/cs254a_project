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

module VX_vpu_decode import VX_gpu_pkg::*, VX_vpu_pkg::*; (
    input  wire [31:0]  instr_i,
    output vpu_decode_t vl_o,
    output vpu_decode_t vs_o,
    output vpu_decode_t vop_o
);

    `UNUSED_VAR (instr_i)

    always @(*) begin
        vl_o = 'x;
        vs_o = 'x;
        vop_o = 'x;
    end

    /*wire [2:0]    nf = instr[31:29];
    wire         mew = instr[28];
    wire [1:0]   mop = instr[27:26];
    wire          vm = instr[25];
    wire [5:0] funct6= instr[31:26];
    wire [1:0]  vset = instr[31:30];
    wire [7:0]  zimm = instr[27:20];*/

    /*vpu_states_t [`NUM_WARPS-1:0] vpu_states;
    `UNUSED_VAR (vpu_states)

    always @(posedge clk) begin
        if (reset) begin
            vpu_states <= '0;
        end else begin
            if (vpu_seq_csr_if.valid) begin
                vpu_states[vpu_seq_csr_if.wid] <= vpu_seq_csr_if.data;
            end
        end
    end*/

    /*reg [INST_VPU_OP_BITS-1:0] vop_type;
    always @(*) begin
        case (funct6)
            6'd0:  vop_type = INST_OP_BITS'(INST_VPU_VADD);
            6'd2:  vop_type = INST_OP_BITS'(INST_VPU_VSUB);
            6'd4:  vop_type = INST_OP_BITS'(INST_VPU_VMINU);
            6'd5:  vop_type = INST_OP_BITS'(INST_VPU_VMIN);
            6'd6:  vop_type = INST_OP_BITS'(INST_VPU_VMAXU);
            6'd7:  vop_type = INST_OP_BITS'(INST_VPU_VMAX);
            6'd9:  vop_type = INST_OP_BITS'(INST_ALU_AND);
            6'd10: vop_type = INST_OP_BITS'(INST_ALU_OR);
            6'd11: vop_type = INST_OP_BITS'(INST_ALU_XOR);
            6'd24: vop_type = INST_OP_BITS'(INST_VPU_VMSEQ);
            6'd25: vop_type = INST_OP_BITS'(INST_VPU_VMSNE);
            6'd26: vop_type = INST_OP_BITS'(INST_ALU_SLTU);
            6'd27: vop_type = INST_OP_BITS'(INST_ALU_SLT);
            6'd28: vop_type = INST_OP_BITS'(INST_VPU_VMSLEU);
            6'd29: vop_type = INST_OP_BITS'(INST_VPU_VMSLE);
            6'd30: vop_type = INST_OP_BITS'(INST_VPU_VMSGTU);
            6'd31: vop_type = INST_OP_BITS'(INST_VPU_VMSGT);
            default: vop_type = 'x;
        endcase
    end*/

    /*
    ex_type = EX_VPU;
                    op_type = INST_OP_BITS'(INST_VPU_VLD);
                    op_args.vpu.vls.nf = nf;
                    op_args.vpu.vls.mop = mop;
                    op_args.vpu.vls.mew = mew;
                    op_args.vpu.vls.width = funct3;
                    `USED_IREG (rs1);
                    `USED_VREG (rd);
                    case (mop)
                        2'b00: begin
                            op_args.vpu.vls.umop = rs2;
                        end
                        2'b10: begin
                            `USED_IREG (rs2);
                        end
                        default: begin
                            `USED_VREG (rs2);
                        end
                    endcase

    */

    /*
    ex_type = EX_VPU;
                    op_type = INST_OP_BITS'(INST_VPU_VST);
                    op_args.vpu.vls.nf = nf;
                    op_args.vpu.vls.mop = mop;
                    op_args.vpu.vls.mew = mew;
                    op_args.vpu.vls.width = funct3;
                    `USED_IREG (rs1);
                    `USED_VREG (rs3);
                    case (mop)
                        2'b00: begin
                            op_args.vpu.vls.umop = rs2;
                        end
                        2'b10: begin
                            `USED_IREG (rs2);
                        end
                        default: begin
                            `USED_VREG (rs2);
                        end
                    endcase
    */

    /*
    ex_type = EX_VPU;
                case (funct3)
                    3'd0, 3'd1, 3'd2: begin // OPIVV, OPFVV, OPMVV
                        op_type = INST_OP_BITS'(INST_VPU_VOP);
                        op_args.vpu.vop.op = vop_type;
                        op_args.vpu.vop.vm = vm;
                        op_args.vpu.vop.use_imm = 0;
                        `USED_VREG (RC_RS1);
                        `USED_VREG (RC_RS2);
                        `USED_VREG (RC_RD);
                    end
                    3'd3: begin // OPIVI
                        op_type = INST_OP_BITS'(INST_VPU_VOP);
                        op_args.vpu.vop.op = vop_type;
                        op_args.vpu.vop.vm = vm;
                        op_args.vpu.vop.use_imm = 1;
                        op_args.vpu.vop.imm = rs1;
                        `USED_VREG (RC_RS2);
                        `USED_VREG (RC_RD);
                    end
                    3'd4, 3'd6: begin // OPIVX, OPIVV
                        op_type = INST_OP_BITS'(INST_VPU_VOP);
                        op_args.vpu.vop.op = vop_type;
                        op_args.vpu.vop.vm = vm;
                        op_args.vpu.vop.use_imm = 0;
                        `USED_IREG (RC_RS1);
                        `USED_VREG (RC_RS2);
                        `USED_VREG (RC_RD);
                    end
                    3'd5: begin // OPFVF
                        op_type = INST_OP_BITS'(INST_VPU_VOP);
                        op_args.vpu.vop.op = vop_type;
                        op_args.vpu.vop.vm = vm;
                        op_args.vpu.vop.use_imm = 0;
                        `USED_FREG (RC_RS1);
                        `USED_VREG (RC_RS2);
                        `USED_VREG (RC_RD);
                    end
                    3'd7: begin // VSETX
                        op_type = INST_OP_BITS'(INST_VPU_VSET);
                        op_args.vpu.vset.vset = vset;
                        op_args.vpu.vset.use_imm = 0;
                        op_args.vpu.vset.use_zimm = 0;
                        `USED_IREG (RC_RD);
                        if(vset == 2'b10) begin
                            `USED_IREG (RC_RS2);
                            `USED_IREG (RC_RS1);
                        end else begin
                            op_args.vpu.vset.use_zimm = 1;
                            op_args.vpu.vset.zimm = zimm;
                            if (vset[0]) begin
                                op_args.vpu.vset.use_imm = 1;
                                op_args.vpu.vset.imm = rs1;
                            end else begin
                                `USED_IREG (RC_RS1);
                            end
                        end
                    end
                endcase
    */

endmodule

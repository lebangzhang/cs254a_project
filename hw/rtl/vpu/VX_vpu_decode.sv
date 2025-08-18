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

module VX_vpu_decode_vl import VX_gpu_pkg::*, VX_vpu_pkg::*; (
    input  wire [31:0]  instr_i,
    output vpu_decode_t decode_o
);
    wire [2:0]     nf = instr_i[31:29];
    wire          mew = instr_i[28];
    wire [1:0]    mop = instr_i[27:26];
    wire           vm = instr_i[25];
    wire [4:0]   umop = instr_i[24:20];
    wire [4:0]    rs2 = instr_i[24:20];
    wire [4:0]    rs1 = instr_i[19:15];
    wire [2:0]  width = instr_i[14:12];
    wire [4:0]     rd = instr_i[11:7];
    wire [6:0] opcode = instr_i[6:0];

    `UNUSED_VAR (opcode)

    reg [INST_LSU_BITS-1:0] op;
    always @* begin
        case (width)
        3'b000: op  = INST_LSU_LB; // 8-bit
        3'b001: op  = INST_LSU_LH; // 16-bit
        3'b010: op  = INST_LSU_LW; // 32-bit
        3'b011: op  = INST_LSU_LD; // 64-bit
        default: op = 'x;
        endcase
    end

    reg [NUM_SRC_OPDS:0][NUM_REGS_BITS-1:0] reg_ids;
    reg [NUM_SRC_OPDS:0] use_regs;

    wire rd_is_float   = (width == 3'b001 || width == 3'b010 || width == 3'b011 || width == 3'b100);
    wire rs2_is_vector = (mop != 2'b10);
    wire rs2_enable    = (mop != 2'b00);

    vpu_decode_t d;
    always @* begin
        d = 'x;
        d.is_masked = vm;
        d.ex_type = EX_LSU;
        d.op_type = op;
        d.op_args.lsu.is_store = 0;
        d.op_args.lsu.offset = 12'({mew, umop, mop, nf});
        d.op_args.lsu.is_float = rd_is_float;
        `USED_REG (rd_is_float ? REG_TYPE_F : REG_TYPE_I, rd, 1);
        `USED_IREG (rs1);
        `USED_REG (rs2_is_vector ? REG_TYPE_V : REG_TYPE_I, rs2, rs2_enable);
        d.reg_ids = reg_ids;
        d.use_regs = use_regs;
    end

    assign decode_o = d;

endmodule

module VX_vpu_decode_vs import VX_gpu_pkg::*, VX_vpu_pkg::*; (
    input  wire [31:0]  instr_i,
    output vpu_decode_t decode_o
);
    wire [2:0]     nf = instr_i[31:29];
    wire          mew = instr_i[28];
    wire [1:0]    mop = instr_i[27:26];
    wire           vm = instr_i[25];
    wire [4:0]   umop = instr_i[24:20];
    wire [4:0]    rs2 = instr_i[24:20];
    wire [4:0]    rs1 = instr_i[19:15];
    wire [2:0]  width = instr_i[14:12];
    wire [4:0]    rs3 = instr_i[11:7];
    wire [6:0] opcode = instr_i[6:0];

    `UNUSED_VAR (opcode)

    reg [INST_LSU_BITS-1:0] op;
    always @* begin
        case (width)
        3'b000: op  = INST_LSU_SB; // 8-bit
        3'b001: op  = INST_LSU_SH; // 16-bit
        3'b010: op  = INST_LSU_SW; // 32-bit
        3'b011: op  = INST_LSU_SD; // 64-bit
        default: op = 'x;
        endcase
    end

    reg [NUM_SRC_OPDS:0][NUM_REGS_BITS-1:0] reg_ids;
    reg [NUM_SRC_OPDS:0] use_regs;

    wire rs3_is_float  = (width == 3'b001 || width == 3'b010 || width == 3'b011 || width == 3'b100);
    wire rs2_is_vector = (mop != 2'b10);
    wire rs2_enable    = (mop != 2'b00);

    vpu_decode_t d;
    always @* begin
        d = 'x;
        d.is_masked = vm;
        d.ex_type = EX_LSU;
        d.op_type = op;
        d.op_args.lsu.is_store = 1;
        d.op_args.lsu.offset = 12'({mew, umop, mop, nf});
        d.op_args.lsu.is_float = rs3_is_float;
        `USED_REG (rs3_is_float ? REG_TYPE_F : REG_TYPE_I, rs3, 1);
        `USED_IREG (rs1);
        `USED_REG (rs2_is_vector ? REG_TYPE_V : REG_TYPE_I, rs2, rs2_enable);
        d.reg_ids = reg_ids;
        d.use_regs = use_regs;
    end

    assign decode_o = d;

endmodule

module VX_vpu_decode_vop import VX_gpu_pkg::*, VX_vpu_pkg::*; (
    input  wire [31:0]  instr_i,
    output vpu_decode_t decode_o
);
    wire [5:0] funct6 = instr_i[31:26];
    wire           vm = instr_i[25];
    wire [4:0]    rs2 = instr_i[24:20];
    wire [4:0]    rs1 = instr_i[19:15];
    wire [2:0] funct3 = instr_i[14:12];
    wire [4:0]     rd = instr_i[11:7];
    wire [6:0] opcode = instr_i[6:0];

    wire [11:0]  zimm = instr_i[31:20];

    `UNUSED_VAR (opcode)

    reg [NUM_SRC_OPDS:0][NUM_REGS_BITS-1:0] reg_ids;
    reg [NUM_SRC_OPDS:0] use_regs;

    reg [INST_ALU_BITS-1:0] alu_op_type;
    reg [ALU_TYPE_BITS-1:0] alu_xtype;
    reg [INST_FPU_BITS-1:0] fpu_op_type;

    always @(*) begin
        case (funct6)
            6'b000000: begin alu_op_type = INST_ALU_BITS'(INST_ALU_ADD); alu_xtype = ALU_TYPE_ARITH; end
            6'b000010: begin alu_op_type = INST_ALU_BITS'(INST_ALU_SUB); alu_xtype = ALU_TYPE_ARITH; end
            6'b001001: begin alu_op_type = INST_ALU_BITS'(INST_ALU_AND); alu_xtype = ALU_TYPE_ARITH; end
            6'b001010: begin alu_op_type = INST_ALU_BITS'(INST_ALU_OR); alu_xtype = ALU_TYPE_ARITH; end
            6'b001011: begin alu_op_type = INST_ALU_BITS'(INST_ALU_XOR); alu_xtype = ALU_TYPE_ARITH; end
            6'b011010: begin alu_op_type = INST_ALU_BITS'(INST_ALU_SLTU); alu_xtype = ALU_TYPE_ARITH; end
            6'b011011: begin alu_op_type = INST_ALU_BITS'(INST_ALU_SLT); alu_xtype = ALU_TYPE_ARITH; end
            6'b100000: begin alu_op_type = INST_ALU_BITS'(INST_M_DIVU); alu_xtype = ALU_TYPE_MULDIV; end
            6'b100001: begin alu_op_type = INST_ALU_BITS'(INST_M_DIV); alu_xtype = ALU_TYPE_MULDIV; end
            6'b100010: begin alu_op_type = INST_ALU_BITS'(INST_M_REMU); alu_xtype = ALU_TYPE_MULDIV; end
            6'b100011: begin alu_op_type = INST_ALU_BITS'(INST_M_REM); alu_xtype = ALU_TYPE_MULDIV; end
            6'b100100: begin alu_op_type = INST_ALU_BITS'(INST_M_MULHU); alu_xtype = ALU_TYPE_MULDIV; end
            6'b100101: begin alu_op_type = INST_ALU_BITS'(INST_M_MUL); alu_xtype = ALU_TYPE_MULDIV; end
            6'b100110: begin alu_op_type = INST_ALU_BITS'(INST_M_MULHSU); alu_xtype = ALU_TYPE_MULDIV; end
            6'b100111: begin alu_op_type = INST_ALU_BITS'(INST_M_MULH); alu_xtype = ALU_TYPE_MULDIV; end
            default: begin alu_op_type = 'x; alu_xtype= 'x; end
        endcase
    end

    always @(*) begin
        case (funct6)
            6'b000000: fpu_op_type = INST_FPU_ADD;
            6'b000010: fpu_op_type = INST_FPU_ADD;
            6'b000100: fpu_op_type = INST_FPU_CMP;
            6'b000101: fpu_op_type = INST_FPU_MISC;
            6'b000110: fpu_op_type = INST_FPU_MISC;
            6'b000111: fpu_op_type = INST_FPU_MISC;
            6'b011010: fpu_op_type = INST_FPU_CMP;
            6'b011011: fpu_op_type = INST_FPU_CMP;
            default: fpu_op_type = 'x;
        endcase
    end

     vpu_decode_t d;
    always @* begin
        d = 'x;
        d.is_masked = vm;

        case (funct3)
        3'b000: begin // OPIVV
            d.ex_type = EX_ALU;
            d.op_type = alu_op_type;
            d.op_args.alu.xtype = alu_xtype;
            d.op_args.alu.use_PC = 0;
            d.op_args.alu.use_imm = 0;
            d.op_args.alu.is_w  = 0;
            d.op_args.alu.imm20 = 'x;
            `USED_VREG (rs1);
            `USED_VREG (rs2);
            `USED_VREG (rd);
        end
        3'b001: begin // OPFVV
            d.ex_type = EX_FPU;
            d.op_type = fpu_op_type;
            d.op_args.fpu.frm = 0;
            d.op_args.fpu.fmt = 0;
            `USED_VREG (rs1);
            `USED_VREG (rs2);
            `USED_VREG (rd);
        end
        3'b010: begin // OPMVV
            d.ex_type = EX_ALU;
            d.op_type = alu_op_type;
            d.op_args.alu.xtype = alu_xtype;
            d.op_args.alu.use_PC = 0;
            d.op_args.alu.use_imm = 0;
            d.op_args.alu.is_w  = 0;
            d.op_args.alu.imm20 = 'x;
            `USED_VREG (rs1);
            `USED_VREG (rs2);
            `USED_VREG (rd);
        end
        3'b011: begin // OPIVI
            d.ex_type = EX_ALU;
            d.op_type = alu_op_type;
            d.op_args.alu.xtype = alu_xtype;
            d.op_args.alu.use_PC = 0;
            d.op_args.alu.use_imm = 1;
            d.op_args.alu.is_w  = 0;
            d.op_args.alu.imm20 = 20'(rs1);
            `USED_VREG (rs2);
            `USED_VREG (rd);
        end
        3'b100: begin // OPIVX
            d.ex_type = EX_ALU;
            d.op_type = alu_op_type;
            d.op_args.alu.xtype = alu_xtype;
            d.op_args.alu.use_PC = 0;
            d.op_args.alu.use_imm = 0;
            d.op_args.alu.is_w  = 0;
            d.op_args.alu.imm20 = 'x;
            `USED_IREG (rs1);
            `USED_VREG (rs2);
            `USED_VREG (rd);
        end
        3'b101: begin // OPFVF
            d.ex_type = EX_FPU;
            d.op_type = fpu_op_type;
            d.op_args.fpu.frm = 0;
            d.op_args.fpu.fmt = 0;
            `USED_FREG (rs1);
            `USED_VREG (rs2);
            `USED_VREG (rd);
        end
        3'b110: begin // OPMVX
            d.ex_type = EX_ALU;
            d.op_type = alu_op_type;
            d.op_args.alu.xtype = alu_xtype;
            d.op_args.alu.use_PC = 0;
            d.op_args.alu.use_imm = 0;
            d.op_args.alu.is_w  = 0;
            d.op_args.alu.imm20 = 'x;
            `USED_IREG (rs1);
            `USED_VREG (rs2);
            `USED_VREG (rd);
        end
        3'b111: begin // OPCFG
            d.op_type = EX_SFU;
            d.op_args.vset.use_imm = 0;
            d.op_args.vset.use_zimm = 0;
            d.op_args.vset.zimm = zimm;
            `USED_IREG (rd);
            if (zimm[11:10] == 2'b10) begin
                `USED_IREG (rs2);
                `USED_IREG (rs1);
            end else begin
                d.op_args.vset.use_zimm = 1;
                if (zimm[10]) begin
                    d.op_args.vset.use_imm = 1;
                    d.op_args.vset.imm = rs1;
                end else begin
                    `USED_IREG (rs1);
                end
            end
        end
        endcase

        d.reg_ids = reg_ids;
        d.use_regs = use_regs;
    end

    assign decode_o = d;

endmodule

module VX_vpu_decode import VX_gpu_pkg::*, VX_vpu_pkg::*; (
    input  wire [31:0]  instr_i,
    output vpu_decode_t vl_o,
    output vpu_decode_t vs_o,
    output vpu_decode_t vop_o
);

    VX_vpu_decode_vl vl_decode (
        .instr_i(instr_i),
        .decode_o(vl_o)
    );

    VX_vpu_decode_vs vs_decode (
        .instr_i(instr_i),
        .decode_o(vs_o)
    );

    VX_vpu_decode_vop vop_decode (
        .instr_i(instr_i),
        .decode_o(vop_o)
    );

endmodule

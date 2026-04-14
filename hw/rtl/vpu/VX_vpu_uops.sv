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

//
// VPU uop expander.
//
module VX_vpu_uops import VX_vpu_pkg::*, VX_gpu_pkg::*; (
    input clk,
    input reset,

    VX_vpu_seq_csr_if.master vpu_seq_csr_if,
    VX_vpu_seq_opc_if.slave  vpu_seq_opc_if,

    input  ibuffer_t ibuf_in,
    output ibuffer_t ibuf_out,

    input  wire start,
    input  wire advance,
    input  wire [UOP_CTR_W-1:0] uop_idx,
    output wire [UOP_CTR_W-1:0] uop_count
);
    `UNUSED_VAR (start)
    `UNUSED_VAR (advance)
    `UNUSED_VAR (vpu_seq_opc_if.wis)

    vpu_csrs_t vpu_csrs;

    always @(posedge clk) begin
        if (reset) begin
            vpu_csrs <= '0;
        end else if (vpu_seq_opc_if.valid) begin
            vpu_csrs.vtype <= vpu_seq_opc_if.data.vtype;
            vpu_csrs.vl    <= vpu_seq_opc_if.data.vl;
        end
    end

    wire csr_has_lmul = (vpu_csrs.vtype.vlmul != 0);

    // Match the legacy expander: 1 uop when LMUL=1, else 8 uops.
    assign uop_count = csr_has_lmul ? UOP_CTR_W'(8) : UOP_CTR_W'(1);

    wire [REG_TYPE_BITS-1:0] rd_type  = get_reg_type(ibuf_in.rd);
    wire [REG_TYPE_BITS-1:0] rs1_type = get_reg_type(ibuf_in.rs1);
    wire [REG_TYPE_BITS-1:0] rs2_type = get_reg_type(ibuf_in.rs2);

    wire [2:0] ctr = uop_idx[2:0];

    ibuffer_t ibuf_r;
    always @(*) begin
        ibuf_r = ibuf_in;
        if (rd_type == REG_TYPE_V) begin
            ibuf_r.rd[4:0] = ibuf_in.rd[4:0] + 5'(ctr);
        end
        if (rs1_type == REG_TYPE_V) begin
            ibuf_r.rs1[4:0] = ibuf_in.rs1[4:0] + 5'(ctr);
        end
        if (rs2_type == REG_TYPE_V) begin
            ibuf_r.rs2[4:0] = ibuf_in.rs2[4:0] + 5'(ctr);
        end
    end

    assign ibuf_out = ibuf_r;
    assign vpu_seq_csr_if.data = vpu_csrs;

    if (UOP_CTR_W > 3) begin : g_unused_upper
        `UNUSED_VAR (uop_idx[UOP_CTR_W-1:3])
    end

endmodule

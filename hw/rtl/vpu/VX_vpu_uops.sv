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

    VX_vpu_seq_csr_if.master vpu_seq_csr_if,
    VX_vpu_seq_opc_if.slave  vpu_seq_opc_if,
    input  wire      start,
    input  wire      next,
    input  ibuffer_t ibuf_in,
    output ibuffer_t ibuf_out,
    output reg       done
);
    `UNUSED_VAR (vpu_seq_opc_if.wis)

    logic [REG_TYPE_BITS-1:0] rd_type  = get_reg_type(ibuf_in.rd);
    logic [REG_TYPE_BITS-1:0] rs1_type = get_reg_type(ibuf_in.rs1);
    logic [REG_TYPE_BITS-1:0] rs2_type = get_reg_type(ibuf_in.rs2);

    reg [2:0] counter;
    vpu_csrs_t vpu_csrs;

    wire csr_has_lmul = (vpu_csrs.vtype.vlmul != 0);

    ibuffer_t ibuf_n;
    always @(*) begin
        ibuf_n = ibuf_in;
        if (rd_type == REG_TYPE_V) begin
            ibuf_n.rd[4:0] = ibuf_in.rd[4:0] + 5'(counter);
        end
        if (rs1_type == REG_TYPE_V) begin
            ibuf_n.rs1[4:0] = ibuf_in.rs1[4:0] + 5'(counter);
        end
        if (rs2_type == REG_TYPE_V) begin
            ibuf_n.rs2[4:0] = ibuf_in.rs2[4:0] + 5'(counter);
        end
    end

    always @(posedge clk) begin
        if (reset) begin
            vpu_csrs <= '0;
        end else if (vpu_seq_opc_if.valid) begin
            vpu_csrs.vtype <= vpu_seq_opc_if.data.vtype;
            vpu_csrs.vl    <= vpu_seq_opc_if.data.vl;
        end
    end

    reg busy;

    always_ff @(posedge clk) begin
        if (reset) begin
            counter <= 0;
            busy    <= 0;
            done    <= 0;
        end else begin
            if (~busy && start) begin
                busy <= 1;
                done <= ~csr_has_lmul;
            end else if (busy && next) begin
                counter <= counter + 3'(csr_has_lmul);
                done <= (counter == 3'd7);
                busy <= ~done;
            end
        end
        ibuf_out <= ibuf_n;
    end

    assign vpu_seq_csr_if.data = vpu_csrs;

endmodule

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

module VX_csr_unit import VX_gpu_pkg::*;
`ifdef EXT_V_ENABLE
    import VX_vpu_pkg::*;
`endif
#(
    parameter `STRING INSTANCE_ID = "",
    parameter CORE_ID = 0,
    parameter NUM_LANES = 1
) (
    input wire                  clk,
    input wire                  reset,

`ifdef PERF_ENABLE
    input sysmem_perf_t         sysmem_perf,
    input pipeline_perf_t       pipeline_perf,
`endif

`ifdef EXT_F_ENABLE
    VX_fpu_csr_if.slave         fpu_csr_if [`NUM_FPU_BLOCKS],
`endif

`ifdef EXT_V_ENABLE
    VX_vpu_seq_csr_if.master    vpu_seq_csr_if [`NUM_WARPS],
`endif

    VX_sched_csr_if.slave       sched_csr_if,
    VX_dcr_csr_if.slave         dcr_csr_if,
    VX_execute_if.slave         execute_if,
    VX_result_if.master         result_if
);
    `UNUSED_SPARAM (INSTANCE_ID)
    localparam PID_BITS = `CLOG2(`NUM_THREADS / NUM_LANES);

    `UNUSED_VAR (execute_if.data.rs3_data)

    reg [NUM_LANES-1:0][`XLEN-1:0]  csr_read_data;
    reg  [`XLEN-1:0]                csr_write_data;
    wire [`XLEN-1:0]                csr_read_data_ro, csr_read_data_rw;
    wire [`XLEN-1:0]                csr_req_data;
    reg                             csr_rd_enable;
    wire                            csr_wr_enable;
    wire                            csr_req_ready;

    wire [`VX_CSR_ADDR_BITS-1:0] csr_addr = execute_if.data.op_args.csr.addr;
    wire [RV_REGS_BITS-1:0] csr_imm = execute_if.data.op_args.csr.imm5;

    wire csr_req_valid = execute_if.valid;
    assign execute_if.ready = csr_req_ready;

    // DCR access bridge
    wire [`VX_CSR_ADDR_BITS-1:0] csr_read_addr = csr_req_valid ? csr_addr : dcr_csr_if.addr;
    wire [7:0] mpm_class = csr_req_valid ? 0 : dcr_csr_if.mpm_class;
    assign dcr_csr_if.ready = ~csr_req_valid;
    assign dcr_csr_if.value = VX_DCR_DATA_WIDTH'(csr_read_data_ro);

    wire [NUM_LANES-1:0][`XLEN-1:0] rs1_data;
    `UNUSED_VAR (rs1_data)
    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_rs1_data
        assign rs1_data[i] = execute_if.data.rs1_data[i];
    end

    wire csr_write_enable = (execute_if.data.op_type == INST_SFU_CSRRW);
`ifdef EXT_V_ENABLE
    wire op_is_vset_w = (execute_if.data.op_type == INST_SFU_VSET);
`else
    wire op_is_vset_w = 1'b0;
`endif

    VX_csr_data #(
        .INSTANCE_ID (INSTANCE_ID),
        .CORE_ID     (CORE_ID)
    ) csr_data (
        .clk            (clk),
        .reset          (reset),

        .mpm_class      (mpm_class),

    `ifdef PERF_ENABLE
        .sysmem_perf    (sysmem_perf),
        .pipeline_perf  (pipeline_perf),
    `endif

        .sched_csr_if   (sched_csr_if),

    `ifdef EXT_F_ENABLE
        .fpu_csr_if     (fpu_csr_if),
    `endif

    `ifdef EXT_V_ENABLE
        .vpu_seq_csr_if  (vpu_seq_csr_if),
    `endif

        .read_enable    (csr_req_valid && csr_rd_enable),
        .read_uuid      (execute_if.data.header.uuid),
        .read_wid       (execute_if.data.header.wid),
        .read_addr      (csr_read_addr),
        .read_data_ro   (csr_read_data_ro),
        .read_data_rw   (csr_read_data_rw),

        .write_enable   (csr_req_valid && csr_wr_enable),
        .write_uuid     (execute_if.data.header.uuid),
        .write_wid      (execute_if.data.header.wid),
        .write_addr     (csr_addr),
        .write_data     (csr_write_data)
    );

    // CSR read

    wire [NUM_LANES-1:0][`XLEN-1:0] wtid, gtid;

    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_wtid
        if (PID_BITS != 0) begin : g_pid
            assign wtid[i] = `XLEN'(execute_if.data.header.pid * NUM_LANES + i);
        end else begin : g_no_pid
            assign wtid[i] = `XLEN'(i);
        end
    end

    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_gtid
        assign gtid[i] = (`XLEN'(CORE_ID) << (NW_BITS + NT_BITS)) + (`XLEN'(execute_if.data.header.wid) << NT_BITS) + wtid[i];
    end

    // Per-lane CTA thread IDs
    wire [NUM_LANES-1:0][`XLEN-1:0] cta_tid_x, cta_tid_y, cta_tid_z;
    for (genvar i = 0; i < NUM_LANES; ++i) begin : g_cta_tid
        wire [CTA_TID_WIDTH:0] tx = (CTA_TID_WIDTH+1)'(sched_csr_if.cta_csrs.thread_idx[0]) + (CTA_TID_WIDTH+1)'(wtid[i]);
        wire cx = (tx >= sched_csr_if.cta_csrs.block_dim[0]);
        wire [CTA_TID_WIDTH:0] ty = (CTA_TID_WIDTH+1)'(sched_csr_if.cta_csrs.thread_idx[1]) + (CTA_TID_WIDTH+1)'(cx);
        wire cy = (ty >= sched_csr_if.cta_csrs.block_dim[1]);
        assign cta_tid_x[i] = cx ? `XLEN'(tx) - `XLEN'(sched_csr_if.cta_csrs.block_dim[0]) : `XLEN'(tx);
        assign cta_tid_y[i] = cy ? `XLEN'(ty) - `XLEN'(sched_csr_if.cta_csrs.block_dim[1]) : `XLEN'(ty);
        assign cta_tid_z[i] = `XLEN'(sched_csr_if.cta_csrs.thread_idx[2]) + `XLEN'(cy);
    end

    always @(*) begin
        csr_rd_enable = 0;
        case (csr_addr)
        `VX_CSR_THREAD_ID       : csr_read_data = wtid;
        `VX_CSR_MHARTID         : csr_read_data = gtid;
        `VX_CSR_CTA_THREAD_ID_X : csr_read_data = cta_tid_x;
        `VX_CSR_CTA_THREAD_ID_Y : csr_read_data = cta_tid_y;
        `VX_CSR_CTA_THREAD_ID_Z : csr_read_data = cta_tid_z;
        default : begin
            csr_read_data = {NUM_LANES{csr_read_data_ro | csr_read_data_rw}};
            csr_rd_enable = ~op_is_vset_w;
        end
        endcase
    end

    // CSR write

    assign csr_req_data = execute_if.data.op_args.csr.use_imm ? `XLEN'(csr_imm) : rs1_data[0];
    assign csr_wr_enable = (csr_write_enable || (| csr_req_data)) && ~op_is_vset_w;

    always @(*) begin
        case (execute_if.data.op_type)
            INST_SFU_CSRRW: begin
                csr_write_data = csr_req_data;
            end
            INST_SFU_CSRRS: begin
                csr_write_data = csr_read_data_rw | csr_req_data;
            end
            //INST_SFU_CSRRC
            default: begin
                csr_write_data = csr_read_data_rw & ~csr_req_data;
            end
        endcase
    end

`ifdef EXT_V_ENABLE
    //
    // VSET compute + per-warp vtype/vl writeback
    //
    // Stage-1 implementation: compute new vtype/vl from vset_args_t and the
    // source operands, store per-warp, broadcast on vpu_seq_csr_if[wid].
    // rd writeback for vset is the new vl replicated across lanes.
    //

    vpu_csrs_t [`NUM_WARPS-1:0] vpu_csrs_r;
    for (genvar w = 0; w < `NUM_WARPS; ++w) begin : g_vpu_seq_csr_drv
        assign vpu_seq_csr_if[w].data = vpu_csrs_r[w];
    end

    wire is_vset = (execute_if.data.op_type == INST_SFU_VSET);

    // vset args live in op_args.vset (alias of vset field).
    wire [11:0] vset_zimm = execute_if.data.op_args.vset.zimm;
    wire [4:0]  vset_imm  = execute_if.data.op_args.vset.imm;
    wire        vset_use_imm  = execute_if.data.op_args.vset.use_imm;
    wire        vset_use_zimm = execute_if.data.op_args.vset.use_zimm;

    // Select vtype source:
    //   vsetivli (use_imm=1, use_zimm=1) -> zimm
    //   vsetvli  (use_imm=0, use_zimm=1) -> zimm
    //   vsetvl   (use_imm=0, use_zimm=0) -> rs2_data[0]
    wire [`XLEN-1:0] vtype_src = vset_use_zimm
                               ? `XLEN'(vset_zimm)
                               : execute_if.data.rs2_data[0];
    `UNUSED_VAR (vtype_src)

    wire [2:0] new_vlmul = vtype_src[2:0];
    wire [2:0] new_vsew  = vtype_src[5:3];
    wire       new_vta   = vtype_src[6];
    wire       new_vma   = vtype_src[7];

    wire [VL_MAX_W-1:0] new_vlmax = vlmax_cacl(new_vlmul, new_vsew);
    wire                new_vill  = vill_calc(new_vsew, new_vlmax);

    // Determine new vl (before vill zeroing and clamping).
    // For vset, rd/rs1 zero flags come from decode (captured from insn register fields).
    wire rd_is_zero  = execute_if.data.op_args.vset.rd_zero;
    wire rs1_is_zero = execute_if.data.op_args.vset.rs1_zero;

    wire [NW_WIDTH-1:0] vset_wid = execute_if.data.header.wid;
    wire [VL_MAX_W-1:0] old_vl   = vpu_csrs_r[vset_wid].vl;

    // rs1_data interpreted as AVL (zero-ext/trunc to VL_MAX_W).
    wire [`XLEN-1:0] rs1_avl_x = rs1_data[0];
    `UNUSED_VAR (rs1_avl_x)
    wire [VL_MAX_W-1:0] rs1_avl = VL_MAX_W'(rs1_avl_x);

    reg [VL_MAX_W-1:0] vl_pre_clamp;
    always @(*) begin
        if (vset_use_imm) begin
            // vsetivli: vl = uimm (rs1 field holds 5-bit uimm)
            vl_pre_clamp = VL_MAX_W'({1'b0, vset_imm});
        end else begin
            if (!rs1_is_zero) begin
                vl_pre_clamp = rs1_avl;
            end else if (!rd_is_zero) begin
                vl_pre_clamp = new_vlmax;
            end else begin
                vl_pre_clamp = old_vl;
            end
        end
    end

    // Clamp to vlmax.
    wire [VL_MAX_W-1:0] vl_clamped = (vl_pre_clamp > new_vlmax) ? new_vlmax : vl_pre_clamp;

    // Apply vill zeroing.
    wire [VL_MAX_W-1:0] new_vl = new_vill ? VL_MAX_W'(0) : vl_clamped;

    vpu_csrs_t new_vpu_csrs;
    always @(*) begin
        new_vpu_csrs        = vpu_csrs_r[vset_wid];
        new_vpu_csrs.vstart = '0;
        new_vpu_csrs.vl     = new_vl;
        if (new_vill) begin
            new_vpu_csrs.vtype.vill  = 1'b1;
            new_vpu_csrs.vtype.reserved = '0;
            new_vpu_csrs.vtype.vma   = 1'b0;
            new_vpu_csrs.vtype.vta   = 1'b0;
            new_vpu_csrs.vtype.vsew  = 3'b000;
            new_vpu_csrs.vtype.vlmul = 3'b000;
        end else begin
            new_vpu_csrs.vtype.vill  = 1'b0;
            new_vpu_csrs.vtype.reserved = '0;
            new_vpu_csrs.vtype.vma   = new_vma;
            new_vpu_csrs.vtype.vta   = new_vta;
            new_vpu_csrs.vtype.vsew  = new_vsew;
            new_vpu_csrs.vtype.vlmul = new_vlmul;
        end
    end

    // Accept on the same handshake as the response buffer (csr_req_ready).
    wire vset_commit = csr_req_valid && csr_req_ready && is_vset;

    always @(posedge clk) begin
        if (reset) begin
            for (int w = 0; w < `NUM_WARPS; ++w) begin
                vpu_csrs_r[w] <= '0;
            end
        end else if (vset_commit) begin
            vpu_csrs_r[vset_wid] <= new_vpu_csrs;
        end
    end

    // Writeback: rd = new vl, replicated to all lanes.
    reg [NUM_LANES-1:0][`XLEN-1:0] vset_wb_data;
    always @(*) begin
        for (int i = 0; i < NUM_LANES; ++i) begin
            vset_wb_data[i] = `XLEN'(new_vl);
        end
    end

    wire [NUM_LANES-1:0][`XLEN-1:0] rsp_data_mux = is_vset ? vset_wb_data : csr_read_data;
`else
    wire [NUM_LANES-1:0][`XLEN-1:0] rsp_data_mux = csr_read_data;
`endif

    VX_elastic_buffer #(
        .DATAW ($bits(sfu_result_t)),
        .SIZE  (2)
    ) rsp_buf (
        .clk       (clk),
        .reset     (reset),
        .valid_in  (csr_req_valid),
        .ready_in  (csr_req_ready),
        .data_in   ({execute_if.data.header, rsp_data_mux}),
        .data_out  (result_if.data),
        .valid_out (result_if.valid),
        .ready_out (result_if.ready)
    );

endmodule

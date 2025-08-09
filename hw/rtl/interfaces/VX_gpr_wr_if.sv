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

interface VX_gpr_wr_if import VX_gpu_pkg::*; #(
    parameter DATA_SIZE   = 1,
    parameter REGID_WIDTH = 1,
    parameter ADDR_WIDTH  = 1,
    parameter DATA_WIDTH  = DATA_SIZE * 8
) ();

    typedef struct packed {
        logic [REGID_WIDTH-1:0] rid;
        logic [ADDR_WIDTH-1:0]  addr;
        logic [DATA_WIDTH-1:0]  data;
        logic [DATA_SIZE-1:0]   mask;
    } data_t;

    logic valid;
    data_t data;

    modport master (
        output valid,
        output data,
    );

    modport slave (
        input  valid,
        input  data,
    );

endinterface

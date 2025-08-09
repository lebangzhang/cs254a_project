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

interface VX_vpu_seq_opc_if import VX_gpu_pkg::*, VX_vpu_pkg::*; ();

    logic                valid;
    logic [PER_OPC_NW_BITS-1:0] wis;
    vpu_states_t         data;

    modport master (
        output valid,
        output wis,
        output data
    );

    modport slave (
        input valid,
        input wis,
        input data
    );

endinterface

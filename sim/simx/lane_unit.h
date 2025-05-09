#pragma once

#include "arch.h"
#include "instr.h"
#include "instr_trace.h"
#include <simobject.h>
#include "types.h"

namespace vortex {

class Core;

class LaneUnit : public SimObject<LaneUnit> {
public:

    SimPort<instr_trace_t*> Inputs;
    SimPort<instr_trace_t*> Outputs;

    /* Need a structure for Sequencer_Req and Sequencer_Rsp */
    SimPort<Sequencer_Req> lane_unit_req_ports
    SimPort<Sequencer_Rsp> lane_unit_rsp_ports 

    /* Add scoreboard/table tracking */

    LaneUnit(const SimContext& ctx,

             /* Lane id */ 
            
             Core* core);

    ~LaneUnit();

    void reset();

    void tick();

private:

    // Pass trace to register file 
    /* Need a new vrf slice with 8 banks and Barber Pole Shifting + How Sequencing is handled */
    Operand_Requestor::Ptr operand_requestor_;

    /* Need Vector Functional Units */
    // Clone _vec_unit 

    uint32_t total_stalls_ = 0;
};

}

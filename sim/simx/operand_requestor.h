#pragma once

#include "arch.h"
#include "instr.h"
#include "instr_trace.h"
#include <simobject.h>
#include "types.h"

namespace vortex {

class Core;

class Operand_Requestor : public SimObject<Operand_Requestor> {
public:

    SimPort<instr_trace_t*> Inputs;
    SimPort<instr_trace_t*> Outputs;

    
    /* Add in/out ports */


    Operand_Requestor(const SimContext& ctx,
            const char* name,
            const Arch& arch,
            Core* core);

    ~Operand_Requestor();

    void reset();

    void tick();

private:

    /* Add : Barber Pole Table <--- Add as a function() */
    Ara2_Vgpr_Slice::Ptr vrf_slice_;


    /* Future Work ? */
    /*Arbiter vrf_arbiter;*/
    
    /* Add : Model the 9 NrOperandQueues output */

    uint32_t total_stalls_ = 0;
};

}

#pragma once

#include "arch.h"
#include "instr.h"
#include "instr_trace.h"
#include <simobject.h>
#include "types.h"

namespace vortex {

class Core;

class Ara2Unit : public SimObject<Ara2Unit> {
public:

    SimPort<instr_trace_t*> Inputs;
    SimPort<instr_trace_t*> Outputs;

    Ara2Unit(const SimContext& ctx,
            const char* name,
            const Arch& arch,
            Core* core);

    ~Ara2Unit();

    void reset();

    void tick();

private:

    /* How does it distribute */ 
    /* Perform a broadcast --> Check if own it */
    std::vector<LaneUnit::Ptr> lane_units_;

    /* TODO : Need a Slide Unit */ 
    SlideUnit::Ptr slide_unit_;


    /* TODO : Load Store Unit (Maybe) */
    /* Future todo : Add arbitration between slide and addrgen */


    uint32_t total_stalls_ = 0;
};

}

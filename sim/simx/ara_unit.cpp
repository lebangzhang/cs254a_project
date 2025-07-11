#include "ara_unit.h"
#include "core.h"

using namespace vortex;

// Simulate clock cycles depending on instruction type and element width and #lanes
// VSET = 1 cycle
// Arator instructions take the same amount of time as ALU instructions.
// In general there should be less overall instructions (hence the SIMD vector speedup).
// But, each vector instruction is bigger, and # of lanes greatly effects execution speed.

// Whenever we change VL using imm/VSET, we need to keep track of the new VL and SEW.
// By default, VL is set to MAXVL.
// After determining VL, we use VL and #lanes in order to determine overall cycle time.
// For example, for a vector add with VL=4 and #lanes=2, we will probably take 2 cycles,
// since we can only operate on two elements of the vector each cycle (limited by #lanes).
// SEW (element width) likely affects the cycle time, we can probably observe
// ALU operation cycle time in relation to element width to determine this though.

// The RTL implementation has an unroll and accumulate stage.
// The unroll stage sends vector elements to the appropriate functional unit up to VL,
// limited by the # lanes available.
// The accumulate stage deals with combining the results from the functional units,
// into the destination vector register.
// Which exact pipeline stage does the VPU unroll the vector (decode or execute)?
// Which exact pipeline stage does the VPU accumulate results?

// How do vector loads and stores interact with the cache?
// How about loading and storing scalars in vector registers?
// How does striding affect loads and stores?

AraUnit::AraUnit(const SimContext& ctx, Core* core)
	: FuncUnit(ctx, core, "ara-unit")
  // TOFIX_ARA : In the scenario all lane units are not the same
  /*
  , lane_unit_(NUM_ARA_LANES)
  , lane_req_ports(NUM_ARA_LANES, this)
  , lane_rsp_ports(NUM_ARA_LANES, this)
  */
  , lane_req_ports(1, this)
  , lane_rsp_ports(1, this)
  , lane_unit_(1)
{

  // Create Lane Units
  for(uint32_t i =0; i < 1 ; i++){
    lane_unit_.at(i) = AraLaneUnit::Create();
  }

  // Bind Ports to operand requestor inside the lanes
  for(uint32_t i=0; i < 1; i++){
    this->lane_req_ports.at(i).bind(&lane_unit_.at(i)->lane_req_port);
    lane_unit_.at(i)->lane_rsp_port.bind(&this->lane_rsp_ports.at(i));
   }

}

AraUnit::~AraUnit() {
  //--
}

void AraUnit::reset() {
  //--
}

void AraUnit::tick() {

    /*DT(3, "----- Entered ARA_Unit ------");*/

    for (uint32_t iw = 0; iw < ISSUE_WIDTH; ++iw) {

        // 3. Handle trace by lane response port to prevent deadlock
        auto &lane_0_rsp = this->lane_rsp_ports.at(0);

        if (!lane_0_rsp.empty()){
            DT(3, "Ara-Unit: Response Start (Lane) : req = " << this->lane_req_ports.at(0).size() << " rsp = " << lane_0_rsp.size());
            auto &trace_received = this->lane_rsp_ports.at(0).front();
            Outputs.at(iw).push(trace_received, 2);
            this->lane_rsp_ports.at(0).pop();
        }

        // 0. Check if there is a valid input
        auto& input = Inputs.at(iw);
        if (input.empty())
            return;

        // 1. At each CC, only process 1 instruction
        auto trace = input.front();

        // 2. For now, assume all lane units behave the same way <--- TOFIX_ARA (Check if there is lane biasing or this is good enough apprx)
        // Check if lane is full --> If full, then stall
        auto &lane_0_req = this->lane_req_ports.at(0);
        if(lane_0_req.size() == 8)
            return;

        // Lane 0 has space to receive instruction --> Send request to lane_0
        DT(3, "Ara-Unit: Request Start (Lane) : req = " << this->lane_req_ports.at(0).size() << " rsp = " << lane_0_rsp.size());
        this->lane_req_ports.at(0).push(trace, 1);
        input.pop();
    }

    /*DT(3, "----- Finished ARA_Unit ------");*/

}
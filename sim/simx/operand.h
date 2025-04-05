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

#pragma once

#include "instr_trace.h"

namespace vortex {



/* Requests to Reg File */
struct RegReq {

    uint32_t src_reg_idx;
    
    RegReq(uint32_t _src_reg_idx = 0
    ) : src_reg_idx(_src_reg_idx)
    {}
};


// Probably don't need this 
// TO FIX : Possible Remove this 
/* Response from Reg File */
struct RegRsp {

    uint32_t valid;
    
    RegRsp(uint32_t _valid = 0
    ) : valid(_valid)
    {}
};





class RegFile : public SimObject<RegFile> {

private:

    static constexpr uint32_t NUM_BANKS = 4;

    // Maybe need to make public ??? 
    static constexpr uint32_t NUM_UNITS = 4;

    uint32_t total_stalls_ = 0; 

public:

    std::vector<SimPort<RegReq>> ReqIn;
    std::vector<SimPort<RegRsp>> ReqOut;
    
    RegFile(const SimContext& ctx)
            : SimObject<RegFile>(ctx, "Register File")
            , ReqIn(NUM_UNITS, this)
            , ReqOut(NUM_UNITS, this)
    {
        total_stalls_ = 0;
    }

    virtual ~RegFile() {}
    
    virtual void reset() {
        total_stalls_ = 0;
    }

    virtual void tick() {
    
        if(ReqIn.empty())
            return;

        for(uint32_t i=0; i < NUM_BANKS; i++){

            for(uint32_t j=0; j < NUM_UNITS; j++ ){

                if(!ReqIn.at(i).empty()){

                    auto request = ReqIn.at(i).front();

                    uint32_t bank = request.src_reg_idx % NUM_BANKS;

                    if( (bank == i) ){
                        
                        RegRsp response;
                        ReqOut.at(i).push(response, 1);
                        ReqIn.at(i).pop();

                        break;
                    }
                }
            }
        }
    }

    uint32_t total_stalls() const {
		return total_stalls_;
	}

};





/* Standard OPC Units */
class OpcUnit : public SimObject<OpcUnit> {

private:

    uint32_t total_stalls_ = 0; 
    bool locked = false;
    uint32_t pending_rsp = 0;

public:
    SimPort<instr_trace_t*> Input;
    SimPort<instr_trace_t*> Output;

    SimPort<RegReq> regfile_req_port;
    SimPort<RegRsp> regfile_rsp_port;

 
    OpcUnit(const SimContext& ctx)
            : SimObject<OpcUnit>(ctx, "Standard OPC Unit")
            , Input(this)
            , Output(this)
            , regfile_req_port(this)
            , regfile_rsp_port(this)
    {
        total_stalls_ = 0;
    }

    virtual ~OpcUnit() {}
    
    virtual void reset() {
        total_stalls_ = 0;
    }

    virtual void tick() {

        total_stalls_ += 1;

        if(Input.empty())
            return;

        auto trace = Input.front();

        uint32_t stalls = 0;

        // Get Number of opd to fetch only once
        int opd_to_fetch[3] = {0, 0, 0}; 
        if(!locked) {
            for(int i = 0; i < NUM_SRC_REGS; i++){
                if( (trace->src_regs[i].type != RegType::None) && ((trace->src_regs[i].idx != 0) && (trace->src_regs[i].type == RegType::Integer))) {
                    opd_to_fetch[i] = trace->src_regs[i].idx;
                }
            }
            locked = true;
            pending_rsp = 0;
        }

        // Handle regfile response 
        if(!regfile_rsp_port.empty()){
            regfile_rsp_port.pop();
            pending_rsp -= 1;
        }


        // Send to Reg File 
        for(int i=0; i < NUM_SRC_REGS; i++){
            
            if(opd_to_fetch[i] != 0){

                RegReq opd_request;
                opd_request.src_reg_idx = opd_to_fetch[i];

                /*if(regfile_req_port.empty()){*/
                regfile_req_port.push(opd_request, 1);
                opd_to_fetch[i] = 0;
                /*}*/

                pending_rsp += 1;
            }
        }

        // Once all Opds sent out 
        if(pending_rsp == 0){
            Input.pop();
            /*Output.push(trace, 1);*/
            locked = false;
        }

    }

    uint32_t total_stalls() const {
		return total_stalls_;
	}
};




/* Operand Class */
class Operand : public SimObject<Operand> {

private:

    static constexpr uint32_t NUM_STD_OPC = 4;
	uint32_t total_stalls_ = 0;

    uint32_t round_robin_counter = 0;


public:
    SimPort<instr_trace_t*> Input;
    SimPort<instr_trace_t*> Output;
    std::vector<OpcUnit::Ptr> opc_units_;

    Operand(const SimContext& ctx)
			: SimObject<Operand>(ctx, "Operand")
			, Input(this)
			, Output(this)
            , opc_units_(NUM_STD_OPC)
    {
		total_stalls_ = 0;
      
        // Instantiate Opc Units 
        for(uint32_t i = 0; i < NUM_STD_OPC; i++){
            opc_units_.at(i) = OpcUnit::Create();
        }

        // Instantiate Reg File
        auto regfile = RegFile::Create();

        // Connect Opc to RegFile
        for(uint32_t i = 0; i < NUM_STD_OPC; i++){
            opc_units_.at(i)->regfile_req_port.bind(&(regfile->ReqIn.at(i)));
        } 

        for(uint32_t i = 0; i < NUM_STD_OPC; i++){
            opc_units_.at(i)->regfile_rsp_port.bind(&(regfile->ReqOut.at(i)));
        }


	}

    virtual ~Operand() {}

    virtual void reset() {
		total_stalls_ = 0;
	}

    virtual void tick() {
        
        if(Input.empty())
            return;

        auto trace = Input.front();
        uint32_t stalls = 0;

        // Find free OpcUnit
        for(uint32_t i = 0; i < NUM_STD_OPC; i++){
            
            if(opc_units_.at(i)->Input.empty()){
                opc_units_.at(i)->Input.push(trace, 1);
                Input.pop();
                break;
            }
        }
        /*total_stalls_ += stalls;*/


        // Round Robin to search for output 
        for(uint32_t i = 0; i < NUM_STD_OPC; i++){

            uint32_t counter = (round_robin_counter + i) % NUM_STD_OPC;

            if(!opc_units_.at(counter)->Output.empty()){
                Output.push(trace,1);

                round_robin_counter += 1;
                opc_units_.at(counter)->Output.pop();
                break;
            }

        }

        /*Output.push(trace, 2);*/

        // TO FIX : The Total Stalls 
        /*
        total_stalls_ += stalls;
		Output.push(trace, 2 + stalls);
		DT(3, "pipeline-operands: " << *trace);
        */
    }

	uint32_t total_stalls() const {
		return total_stalls_;
	}
};

}

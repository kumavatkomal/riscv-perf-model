#include "Pegasus.hpp"
#include <stdexcept>

/**
ilimit 
workload
params
db_file
snapshot_threshold
*/

namespace olympia::edm
{ 
   PegasusAdapter::PegasusAdapter(
    uint64_t ilimit,
    std::string &workload,
    std::map<std::string, std::string> & params,
    
   ) {
    cosim_(ilimit, workload)
   }

    bool PegasusAdapter::isFinished(CoreId core_id, HartId hart_id) const
    {
        return cosim_->isFinished(core_id, hart_id);
    }

    Addr PegasusAdapter::peekNextPc(CoreId core_id, HartId hart_id) const
    {
        return cosim_->peekNextPc(core_id, hart_id);
    }

    InstructionInfo PegasusAdapter::step(CoreId core_id, HartId hart_id)
    {
        auto event = cosim_->step(core_id, hart_id);
        return eventToInfo_(event);
    }

    InstructionInfo PegasusAdapter::stepWithOverridePc(CoreId core_id, HartId hart_id, Addr override_pc)
    {
        auto event = cosim_->stepWithOverridePc(core_id, hart_id, override_pc);
        return eventToInfo_(event);
    }

    void PegasusAdapter::commitInstruction(CoreId core_id, HartId hart_id, uint64_t iss_uid)
    {
        cosim_->commitInstruction(core_id, hart_id, iss_uid);
        
        // Clean up any pending branch event for this instruction
        auto it = pending_branch_events_.find(iss_uid);
        if (it != pending_branch_events_.end()) {
            pending_branch_events_.erase(it);
        }
    }

    void PegasusAdapter::commitStoreWrite(CoreId core_id, HartId hart_id, uint64_t iss_uid)
    {
        cosim_->commitStoreWrite(core_id, hart_id, iss_uid);
    }

    void PegasusAdapter::dropStoreWrite(CoreId core_id, HartId hart_id, uint64_t iss_uid)
    {
        cosim_->dropStoreWrite(core_id, hart_id, iss_uid);
    }

    void PegasusAdapter::flush(CoreId core_id, HartId hart_id, const EDMCheckpoint & checkpoint)
    {
        cosim_->flush(core_id, hart_id, checkpoint);
        
        // Clear all pending branch events on flush
        pending_branch_events_.clear();
    }

    InstructionInfo PegasusAdapter::eventToInfo_(const pegasus::cosim::EventAccessor & accessor)
    {
        InstructionInfo info;
        
        // Extract basic instruction information from the event
        info.pc = accessor.getPc();
        info.opcode = accessor.getOpcode();
        info.iss_uid = accessor.getUid();
        info.is_branch = accessor.isBranch();
        info.is_load = accessor.isLoad();
        info.is_store = accessor.isStore();
        
        // Handle branch information
        if (info.is_branch) {
            info.branch_taken = accessor.isBranchTaken();
            info.branch_target = accessor.getBranchTarget();
            
            // Store the event for potential later use
            pending_branch_events_[info.iss_uid] = accessor;
        }
        
        // Handle memory access information
        if (info.is_load || info.is_store) {
            info.mem_addr = accessor.getMemoryAddress();
            info.mem_size = accessor.getMemorySize();
            
            if (info.is_load) {
                info.load_data = accessor.getLoadData();
            } else {
                info.store_data = accessor.getStoreData();
            }
        }
        
        // Extract register information
        info.dest_reg = accessor.getDestReg();
        info.dest_value = accessor.getDestValue();
        
        return info;
    }
}
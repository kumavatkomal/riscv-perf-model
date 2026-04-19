#include "Pegasus.hpp"
#include "EDMTypes.hpp"
#include <cstdint>
#include <map>
#include <memory>
#include "pegasus/cosim/PegasusCoSim.hpp"
#include "pegasus/cosim/EventAccessor.hpp"
#include "edm/EDMFactory.hpp"

namespace olympia::edm
{
    PegasusAdapter::PegasusAdapter(const std::string & workload, uint64_t ilimit,
                                   const std::map<std::string, std::string> & params,
                                   const std::string & db_file, size_t snapshot_threshold) :
        cosim_(std::make_unique<pegasus::cosim::PegasusCoSim>(
            ilimit, workload, params,
            std::vector<std::vector<std::string>>{}, // empty pegasus_loggers
            db_file, snapshot_threshold))
    {
    }

    bool PegasusAdapter::isFinished(CoreId core_id, HartId hart_id) const
    {
        return cosim_->isSimulationFinished(core_id, hart_id);
    }

    Addr PegasusAdapter::peekNextPc(CoreId core_id, HartId hart_id) const
    {
        return cosim_->getPc(core_id, hart_id);
    }

    InstructionInfo PegasusAdapter::step(CoreId core_id, HartId hart_id)
    {
        pegasus::cosim::EventAccessor evt = cosim_->step(core_id, hart_id);
        InstructionInfo info = eventToInfo_(evt);
        pending_events_.emplace(info.iss_uid, std::move(evt));
        return info;
    }

    InstructionInfo PegasusAdapter::stepWithOverridePc(CoreId core_id, HartId hart_id,
                                                       Addr override_pc)
    {
        // TODO: Integrate the checkpoint mechanism
        // maybe right now olympia will automatically flush if there is anything wrong but we need
        // checkpoint mechanism
        pegasus::cosim::EventAccessor evt = cosim_->step(core_id, hart_id, override_pc);
        InstructionInfo info = eventToInfo_(evt);
        info.is_wrong_path = true;
        pending_events_.emplace(info.iss_uid, std::move(evt));
        return info;
    }

    void PegasusAdapter::commitInstruction(CoreId /*core_id*/ , HartId /*hart_id*/, uint64_t iss_uid)
    {
        auto it = pending_events_.find(iss_uid);
        if (it != pending_events_.end())
        {
            cosim_->commit(it->second);
            pending_events_.erase(it);
        } 
    }

    void PegasusAdapter::commitStoreWrite(CoreId /*core_id*/, HartId /*hart_id*/, uint64_t iss_uid)
    {
       auto it = pending_events_.find(iss_uid);
       sparta_assert(it != pending_events_.end(), "commitStoreWrite for iss_uid " << iss_uid << " not found in pending_events");
       cosim_->commitStoreWrite(it->second);
       cosim_->commit(it->second);
       pending_events_.erase(it);
    }

    void PegasusAdapter::dropStoreWrite(CoreId /*core_id*/, HartId /*hart_id*/, uint64_t iss_uid)
    {
        auto it = pending_events_.find(iss_uid);
        // TODO: need to make this finding the instruction a function
        if (it != pending_events_.end())
        {
            cosim_->dropStoreWrite(it->second);
            pending_events_.erase(it);
        }
    }

    void PegasusAdapter::flush(CoreId /*core_id*/, HartId /*hart_id*/,
                               const EDMCheckpoint & checkpoint)
    {
        if (pending_events_.empty())
        {
            return;
        }

        if(checkpoint.iss_uid != std::numeric_limits<uint64_t>::max())
        {
            auto it = pending_events_.find(checkpoint.iss_uid);
            sparta_assert(it != pending_events_.end(), "flush : iss_uid " << checkpoint.iss_uid << "not found in pending_events");
            cosim_->flush(it->second, false);
            pending_events_.erase(it, pending_events_.end());
        } 
        else{
            auto & oldest = pending_events_.begin()->second;
            cosim_->flush(oldest, false);
            pending_events_.clear(); 
        }
    }

    // The private functions eventToInst_ - maybe keep the original name buildToInst_() and make it
    // a part of the EDMInterface
    InstructionInfo PegasusAdapter::eventToInfo_(pegasus::cosim::EventAccessor & evt)
    {
        InstructionInfo info;

        info.iss_uid = evt.getEuid();

        const pegasus::cosim::Event* e = evt.get();
        if (!e)
        {
            return info;
        }

        info.pc = e->getPc();
        info.next_pc = e->getNextPc();
        info.dasm = e->getDisassemblyStr();
        info.opcode = e->getOpcode();
        info.is_branch = e->isChangeOfFlowEvent();
        info.faulted = (e->getExceptionType() != pegasus::ExcpType::INVALID);

        if (info.is_branch)
        {
            info.alt_next_pc = evt->getAltNextPc();
        }

        info.is_load = !evt->getMemoryReads().empty();
        info.is_store = !evt->getMemoryWrites().empty();

        info.ends_simulation = evt->isLastEvent();

        for (const auto & src : evt->getRegisterReads())
        {
            RegAccess r_access;
            r_access.reg_name = src.reg_id.reg_name;
            r_access.value = src.value;
            info.reg_reads.push_back(std::move(r_access));
        }

        for (const auto & dst : evt->getRegisterWrites())
        {
            RegAccess r_access;
            r_access.reg_name = dst.reg_id.reg_name;
            r_access.value = dst.value;
            r_access.prev_value = dst.prev_value;
            info.reg_writes.push_back(std::move(r_access));
        }

        for (const auto & mr : evt->getMemoryReads())
        {
            MemAccess mem;
            mem.paddr = mr.paddr;
            mem.vaddr = mr.vaddr;
            mem.size = mr.size;
            mem.value = mr.value;
            mem.is_write = false;
            info.mem_reads.push_back(std::move(mem));
        }

        for (const auto & mw : evt->getMemoryWrites())
        {
            MemAccess mem;
            mem.paddr = mw.paddr;
            mem.vaddr = mw.vaddr;
            mem.size = mw.size;
            mem.value = mw.value;
            mem.prev_value = mw.prev_value;
            mem.is_write = true;
            info.mem_writes.push_back(std::move(mem));
        }

        return info;
    }

    static olympia::edm::BackendRegistrar<olympia::edm::PegasusAdapter>
        s_pegasus_registrar("pegasus");

} // namespace olympia::edm
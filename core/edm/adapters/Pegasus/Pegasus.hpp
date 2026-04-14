#include "edm/EDMCheckpoint.hpp"
#include "edm/EDMInterface.hpp"
#include "edm/EDMTypes.hpp"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wall"
#include "pegasus/cosim/PegasusCoSim.hpp"
#pragma clang diagnostic pop
#include "pegasus/cosim/EventAccessor.hpp"
#include <memory>
#include <map>

/*
 PegasusAdapter::PegasusAdapter(
    const std::string & workload,
    uint64_t ilimit,
    const std::map<std::string, std::string> & params,
    const std::string & db_file,
    size_t snapshot_threshold
   ) {

   }
*/

namespace olympia::edm
{
    class PegasusAdapter : public EDMInterface 
    {
    public:
        PegasusAdapter(uint64_t ilimit , std::string & workload, std::map<std::string, std::string> & params);

        ~PegasusAdapter() = default;

        bool isFinished(CoreId core_id, HartId hart_id) const override;

        Addr peekNextPc(CoreId core_id, HartId hart_id) const override;

        InstructionInfo step(CoreId core_id, HartId hart_id) override;

        InstructionInfo stepWithOverridePc(CoreId core_id, HartId hart_id, Addr override_pc) override;
        
        void commitInstruction(CoreId core_id, HartId hart_id, uint64_t iss_uid) override;

        void commitStoreWrite(CoreId core_id, HartId hart_id, uint64_t iss_uid) override;

        void dropStoreWrite(CoreId core_id, HartId hart_id, uint64_t iss_uid) override;

        void flush(CoreId core_id, HartId hart_id, const EDMCheckpoint & checkpoint) override;

    private:
        InstructionInfo eventToInfo_(const pegasus::cosim::EventAccessor & accessor);

        std::unique_ptr<pegasus::cosim::PegasusCoSim> cosim_;
        std::map<uint64_t, pegasus::cosim::EventAccessor> pending_branch_events_;
    };
}
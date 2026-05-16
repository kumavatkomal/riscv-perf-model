#pragma once

#include <cstdint>
#include <unordered_map>
#include <string>
#include <vector>

#include "edm/EDMFactory.hpp"

namespace olympia::edm
{
    // Whisper-backed EDM adapter.
    //
    // Current integration mode executes Whisper externally once,
    // captures CSV instruction trace, and serves instructions to Olympia.
    // This gives real Whisper decode/PC semantics while preserving the
    // existing backend-agnostic EDM interface.
    class WhisperEDMAdapter : public EDMInterface
    {
      public:
        WhisperEDMAdapter(const std::string & config_file,
              const std::string & workload,
              const EDMBackendFactory::BackendParams & params);
        ~WhisperEDMAdapter() override;

        bool isFinished(CoreId core_id, HartId hart_id) const override;
        Addr peekNextPc(CoreId core_id, HartId hart_id) const override;

        InstructionInfo step(CoreId core_id, HartId hart_id) override;
        InstructionInfo stepWithOverridePc(CoreId core_id,
                                           HartId hart_id,
                                           Addr override_pc) override;

        void commitInstruction(CoreId core_id, HartId hart_id, uint64_t iss_uid) override;
        void commitStoreWrite(CoreId core_id, HartId hart_id, uint64_t iss_uid) override;
        void dropStoreWrite(CoreId core_id, HartId hart_id, uint64_t iss_uid) override;

        void flush(CoreId core_id,
                   HartId hart_id,
                   const EDMCheckpoint & checkpoint) override;

      private:
        std::string workload_;
        EDMBackendFactory::BackendParams params_;

        std::string whisper_bin_;
        std::string whisper_args_;
        std::string trace_csv_path_;
        bool keep_trace_csv_ = false;

        std::vector<InstructionInfo> trace_;
        std::unordered_map<Addr, std::vector<size_t>> pc_to_indices_;
        size_t cursor_ = 0;

        Addr next_pc_ = 0;
        bool finished_ = false;

        void loadWhisperTrace_();
        void runWhisperToCsv_();
        void parseTraceCsv_();

        bool seekToPc_(Addr pc);

        static std::string getEnvOrDefault_(const char * env_name,
                                            const std::string & default_value);
        static bool parseBool_(const std::string & value, bool default_value);
        static std::string shellQuote_(const std::string & text);
        static std::string trim_(const std::string & text);
        static uint64_t parseInteger_(const std::string & text);
        static std::vector<std::string> splitCsvLine_(const std::string & line);
    };
}

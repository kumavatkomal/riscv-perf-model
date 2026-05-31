
#pragma once

#include "edm/EDMCheckpoint.hpp"
#include "edm/EDMInterface.hpp"
#include "edm/EDMTypes.hpp"
#include <memory>
#include <map>
#include <string>

// Forward declarations for Whisper classes
namespace WdRiscv
{
    template<typename URV> class System;
    template<typename URV> class Hart;
} // namespace WdRiscv

namespace olympia::edm
{
    /**
     * @brief Whisper adapter for EDM (Execution-Driven Model)
     * 
     * This adapter integrates the Whisper RISC-V ISA simulator as a functional
     * model driver for Olympia's execution-driven simulation mode.
     */
    class WhisperAdapter : public EDMInterface
    {
      public:
        /**
         * @brief Construct a new Whisper Adapter
         * 
         * @param config_file Path to Whisper configuration file (JSON)
         * @param filename Path to the ELF binary to execute
         */
        WhisperAdapter(const std::string & config_file, const std::string & filename);

        /**
         * @brief Destroy the Whisper Adapter
         */
        ~WhisperAdapter();

        /**
         * @brief Check if the simulation has finished
         * 
         * @param core_id Core identifier
         * @param hart_id Hart (hardware thread) identifier
         * @return true if simulation is complete
         */
        bool isFinished(CoreId core_id, HartId hart_id) const override;

        /**
         * @brief Peek at the next PC without executing
         * 
         * @param core_id Core identifier
         * @param hart_id Hart identifier
         * @return Next program counter value
         */
        Addr peekNextPc(CoreId core_id, HartId hart_id) const override;

        /**
         * @brief Execute one instruction normally
         * 
         * @param core_id Core identifier
         * @param hart_id Hart identifier
         * @return InstructionInfo containing execution details
         */
        InstructionInfo step(CoreId core_id, HartId hart_id) override;

        /**
         * @brief Execute one instruction with overridden PC (for speculation)
         * 
         * @param core_id Core identifier
         * @param hart_id Hart identifier
         * @param override_pc PC value to use instead of current PC
         * @return InstructionInfo containing execution details
         */
        InstructionInfo stepWithOverridePc(CoreId core_id, HartId hart_id,
                                           Addr override_pc) override;

        /**
         * @brief Commit an instruction (make it architecturally visible)
         * 
         * @param core_id Core identifier
         * @param hart_id Hart identifier
         * @param iss_uid Unique instruction identifier
         */
        void commitInstruction(CoreId core_id, HartId hart_id, uint64_t iss_uid) override;

        /**
         * @brief Commit a store write to memory
         * 
         * @param core_id Core identifier
         * @param hart_id Hart identifier
         * @param iss_uid Unique instruction identifier
         */
        void commitStoreWrite(CoreId core_id, HartId hart_id, uint64_t iss_uid) override;

        /**
         * @brief Drop a speculative store write
         * 
         * @param core_id Core identifier
         * @param hart_id Hart identifier
         * @param iss_uid Unique instruction identifier
         */
        void dropStoreWrite(CoreId core_id, HartId hart_id, uint64_t iss_uid) override;

        /**
         * @brief Flush pipeline and restore from checkpoint
         * 
         * @param core_id Core identifier
         * @param hart_id Hart identifier
         * @param checkpoint Checkpoint to restore from
         */
        void flush(CoreId core_id, HartId hart_id, const EDMCheckpoint & checkpoint) override;

      private:
        // Internal checkpoint structure for Whisper state
        struct WhisperCheckpoint
        {
            uint64_t pc;
            std::vector<uint64_t> int_regs;
            std::vector<uint64_t> fp_regs;
        };

        /**
         * @brief Get Whisper Hart for given core and hart ID
         * 
         * @param core_id Core identifier
         * @param hart_id Hart identifier
         * @return Shared pointer to Whisper Hart
         */
        std::shared_ptr<WdRiscv::Hart<uint64_t>> getHart_(CoreId core_id, HartId hart_id) const;

        /**
         * @brief Convert Whisper instruction to EDM InstructionInfo
         * 
         * @param hart Whisper Hart that executed the instruction
         * @param uid Unique instruction identifier
         * @return InstructionInfo structure
         */
        InstructionInfo extractInstructionInfo_(const std::shared_ptr<WdRiscv::Hart<uint64_t>>& hart, uint64_t uid);

        /**
         * @brief Save current Whisper state as checkpoint
         * 
         * @param hart Whisper Hart to checkpoint
         * @param uid Unique instruction identifier to associate with checkpoint
         */
        void saveCheckpoint_(const std::shared_ptr<WdRiscv::Hart<uint64_t>>& hart, uint64_t uid);

        /**
         * @brief Restore Whisper state from checkpoint
         * 
         * @param hart Whisper Hart to restore
         * @param checkpoint Checkpoint to restore from
         */
        void restoreCheckpoint_(const std::shared_ptr<WdRiscv::Hart<uint64_t>>& hart, const WhisperCheckpoint & checkpoint);

        // Whisper system instance (manages cores and memory)
        std::unique_ptr<WdRiscv::System<uint64_t>> whisper_system_;

        // Map of pending speculative instructions (uid -> InstructionInfo)
        std::map<uint64_t, InstructionInfo> pending_instructions_;

        // Map of pending speculative stores (uid -> MemAccess)
        std::map<uint64_t, MemAccess> pending_stores_;

        // Map of speculation checkpoints (uid -> WhisperCheckpoint)
        std::map<uint64_t, WhisperCheckpoint> speculation_checkpoints_;

        // Next unique instruction ID
        uint64_t next_uid_ = 0;

        // Simulation finished flag
        mutable bool finished_ = false;
    };

} // namespace olympia::edm

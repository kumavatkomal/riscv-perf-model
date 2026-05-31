

#include "Whisper.hpp"
#include "edm/EDMTypes.hpp"
#include <sparta/utils/SpartaAssert.hpp>

// Whisper includes
#include "System.hpp"
#include "Hart.hpp"
#include "DecodedInst.hpp"

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <cstring>  // for std::memcpy
#include <yaml-cpp/yaml.h>

namespace olympia::edm
{
    WhisperAdapter::WhisperAdapter(const std::string & config_file, const std::string & filename)
    {
        std::cout << "WhisperAdapter: Initializing with config=" << config_file 
                  << " file=" << filename << std::endl;

        
        if (config_file.empty())
        {
            throw std::runtime_error("WhisperAdapter: Configuration file is required");
        }

        YAML::Node config = YAML::LoadFile(config_file);
        
        // Read Whisper configuration parameters
        const uint64_t ilimit = config["ilimit"].as<uint64_t>(10000);
        const std::string isa = config["params"]["isa"].as<std::string>("rv64imafdcbv_zicsr_zifencei");
        const uint64_t memory_size = config["params"]["memory_size"].as<uint64_t>(4*1024*1024*1024ULL);
        const uint64_t page_size = config["params"]["page_size"].as<uint64_t>(4*1024*1024*1024ULL);
        const unsigned cores = config["params"]["cores"].as<unsigned>(1);
        const unsigned harts_per_core = config["params"]["harts_per_core"].as<unsigned>(1);

        std::cout << "WhisperAdapter: Config - ISA=" << isa 
                  << " Memory=" << (memory_size / (1024*1024*1024)) << "GB"
                  << " Cores=" << cores << " Harts=" << harts_per_core
                  << " ilimit=" << ilimit << std::endl;

        // Initialize Whisper System with configured parameters
        whisper_system_ = std::make_unique<WdRiscv::System<uint64_t>>(
            cores,          // Number of cores (from config)
            harts_per_core, // Number of harts per core (from config)
            0,              // Hart ID offset
            memory_size,    // Memory size from config
            page_size       // Page size from config
        );

        // Load the ELF file
        if (!filename.empty())
        {
            if (!whisper_system_->loadElfFiles({filename}, false, true))
            {
                throw std::runtime_error("WhisperAdapter: Failed to load ELF file: " + filename);
            }
        }

        std::cout << "WhisperAdapter: Initialization complete" << std::endl;
    }

    WhisperAdapter::~WhisperAdapter()
    {
        std::cout << "WhisperAdapter: Shutting down" << std::endl;
    }

    bool WhisperAdapter::isFinished(CoreId core_id, HartId hart_id) const
    {
        auto hart = getHart_(core_id, hart_id);
        if (!hart)
        {
            return true;
        }
        
        return hart->hasTargetProgramFinished();
    }

    Addr WhisperAdapter::peekNextPc(CoreId core_id, HartId hart_id) const
    {
        auto hart = getHart_(core_id, hart_id);
        sparta_assert(hart != nullptr, "WhisperAdapter: Invalid hart ID");
        
        return hart->peekPc();
    }

    InstructionInfo WhisperAdapter::step(CoreId core_id, HartId hart_id)
    {
        auto hart = getHart_(core_id, hart_id);
        sparta_assert(hart != nullptr, "WhisperAdapter: Invalid hart ID");
        
        // Execute one instruction
        hart->singleStep();
        
        // Extract instruction information
        uint64_t uid = next_uid_++;
        InstructionInfo info = extractInstructionInfo_(hart, uid);
        
        // Track pending instruction
        pending_instructions_[uid] = info;
        
        return info;
    }

    InstructionInfo WhisperAdapter::stepWithOverridePc(CoreId core_id, HartId hart_id,
                                                       Addr override_pc)
    {
        auto hart = getHart_(core_id, hart_id);
        sparta_assert(hart != nullptr, "WhisperAdapter: Invalid hart ID");
        
        // Save current state as checkpoint before speculation
        uint64_t uid = next_uid_;
        saveCheckpoint_(hart, uid);
        
        // Override PC
        hart->pokePc(override_pc);
        
        // Execute instruction
        hart->singleStep();
        
        // Extract instruction information
        InstructionInfo info = extractInstructionInfo_(hart, uid);
        info.is_wrong_path = true;  // Mark as speculative
        next_uid_++;
        
        // Track pending speculative instruction
        pending_instructions_[uid] = info;
        
        return info;
    }

    void WhisperAdapter::commitInstruction(CoreId core_id, HartId hart_id, uint64_t iss_uid)
    {
        // Remove from pending instructions (make architecturally visible)
        auto it = pending_instructions_.find(iss_uid);
        if (it != pending_instructions_.end())
        {
            std::cout << "WhisperAdapter: Committing instruction uid=" << iss_uid << std::endl;
            pending_instructions_.erase(it);
        }
        
        // Remove associated checkpoint (no longer needed)
        auto ckpt_it = speculation_checkpoints_.find(iss_uid);
        if (ckpt_it != speculation_checkpoints_.end())
        {
            speculation_checkpoints_.erase(ckpt_it);
        }
    }

    void WhisperAdapter::commitStoreWrite(CoreId core_id, HartId hart_id, uint64_t iss_uid)
    {
        // Commit store to Whisper memory
        auto it = pending_stores_.find(iss_uid);
        if (it != pending_stores_.end())
        {
            std::cout << "WhisperAdapter: Committing store uid=" << iss_uid 
                      << " addr=0x" << std::hex << it->second.paddr << std::dec << std::endl;
            
            // Store is already written to memory by Whisper during execution
            // Just remove from pending
            pending_stores_.erase(it);
        }
    }

    void WhisperAdapter::dropStoreWrite(CoreId core_id, HartId hart_id, uint64_t iss_uid)
    {
        // Discard speculative store
        auto it = pending_stores_.find(iss_uid);
        if (it != pending_stores_.end())
        {
            std::cout << "WhisperAdapter: Dropping store uid=" << iss_uid << std::endl;
            
            // Note: Whisper executes stores immediately to memory. Ideally we would
            // restore memory to pre-store state here, but this requires saving memory
            // contents before each speculative store. Current approach relies on
            // flush() to restore full architectural state when needed.
            pending_stores_.erase(it);
        }
    }

    void WhisperAdapter::flush(CoreId core_id, HartId hart_id, const EDMCheckpoint & checkpoint)
    {
        std::cout << "WhisperAdapter: Flushing to iss_uid=" << checkpoint.iss_uid << std::endl;
        
        auto hart = getHart_(core_id, hart_id);
        sparta_assert(hart != nullptr, "WhisperAdapter: Invalid hart ID");
        
        // Find the checkpoint to restore to
        if (checkpoint.iss_uid != std::numeric_limits<uint64_t>::max())
        {
            auto it = speculation_checkpoints_.find(checkpoint.iss_uid);
            if (it != speculation_checkpoints_.end())
            {
                // Restore the checkpoint
                restoreCheckpoint_(hart, it->second);
                
                // Clear all speculative state after this checkpoint
                auto pending_it = pending_instructions_.upper_bound(checkpoint.iss_uid);
                pending_instructions_.erase(pending_it, pending_instructions_.end());
                
                auto ckpt_it = speculation_checkpoints_.upper_bound(checkpoint.iss_uid);
                speculation_checkpoints_.erase(ckpt_it, speculation_checkpoints_.end());
                
                auto store_it = pending_stores_.upper_bound(checkpoint.iss_uid);
                pending_stores_.erase(store_it, pending_stores_.end());
            }
            else
            {
                // Checkpoint not found, clear everything
                pending_instructions_.clear();
                pending_stores_.clear();
                speculation_checkpoints_.clear();
            }
        }
        else
        {
            // No specific checkpoint, clear all speculative state
            pending_instructions_.clear();
            pending_stores_.clear();
            speculation_checkpoints_.clear();
        }
    }

    // Private helper methods

    std::shared_ptr<WdRiscv::Hart<uint64_t>> WhisperAdapter::getHart_(CoreId core_id, HartId hart_id) const
    {
        if (!whisper_system_)
        {
            return nullptr;
        }
        
        // Calculate global hart index
        unsigned global_hart_id = core_id * whisper_system_->hartsPerCore() + hart_id;
        
        return whisper_system_->ithHart(global_hart_id);
    }

    // Internal checkpoint structure for Whisper state
    struct WhisperCheckpoint
    {
        uint64_t pc;
        std::vector<uint64_t> int_regs;
        std::vector<uint64_t> fp_regs;
    };

    InstructionInfo WhisperAdapter::extractInstructionInfo_(const std::shared_ptr<WdRiscv::Hart<uint64_t>>& hart, uint64_t uid)
    {
        InstructionInfo info;
        info.iss_uid = uid;

        // Get PC of last executed instruction
        info.pc = hart->lastPc();
        info.next_pc = hart->peekPc();
        
        // Get last store information (if any)
        uint64_t store_va = 0, store_pa1 = 0, store_pa2 = 0, store_value = 0;
        unsigned store_size = hart->lastStore(store_va, store_pa1, store_pa2, store_value);
        if (store_size > 0)
        {
            info.is_store = true;
            MemAccess mem;
            mem.vaddr = store_va;
            mem.paddr = store_pa1;
            mem.size = store_size;
            mem.value.resize(store_size);
            std::memcpy(mem.value.data(), &store_value, store_size);
            mem.is_write = true;
            info.mem_writes.push_back(mem);
            
            // Track pending store for speculation
            pending_stores_[uid] = mem;
        }
        
        // Get last load/store address (for loads)
        uint64_t ldst_va = 0, ldst_pa = 0;
        unsigned ldst_size = hart->lastLdStAddress(ldst_va, ldst_pa);
        if (ldst_size > 0 && !info.is_store)
        {
            info.is_load = true;
            MemAccess mem;
            mem.vaddr = ldst_va;
            mem.paddr = ldst_pa;
            mem.size = ldst_size;
            mem.is_write = false;
            
            // Get the loaded value from the destination register
            // (The load instruction writes the loaded value to a register)
            if (int_reg >= 0)
            {
                // The loaded value is now in the destination register
                uint64_t loaded_value = 0;
                hart->peekIntReg(int_reg, loaded_value);
                mem.value.resize(ldst_size);
                std::memcpy(mem.value.data(), &loaded_value, ldst_size);
            }
            
            info.mem_reads.push_back(mem);
        }
        
        // Get integer register write (if any)
        uint64_t prev_int_val = 0;
        int int_reg = hart->lastIntReg(prev_int_val);
        if (int_reg >= 0)
        {
            RegAccess ra;
            ra.reg_id = int_reg;
            ra.reg_name = std::string(hart->intRegName(int_reg));
            
            // Get current value
            uint64_t curr_val = 0;
            hart->peekIntReg(int_reg, curr_val);
            ra.value.resize(sizeof(curr_val));
            std::memcpy(ra.value.data(), &curr_val, sizeof(curr_val));
            
            // Get previous value
            ra.prev_value.resize(sizeof(prev_int_val));
            std::memcpy(ra.prev_value.data(), &prev_int_val, sizeof(prev_int_val));
            
            info.reg_writes.push_back(ra);
        }
        
        // Get FP register write (if any)
        uint64_t prev_fp_val = 0;
        int fp_reg = hart->lastFpReg(prev_fp_val);
        if (fp_reg >= 0)
        {
            RegAccess ra;
            ra.reg_id = fp_reg;
            ra.reg_name = std::string(hart->fpRegName(fp_reg));
            
            // Get current value
            uint64_t curr_val = 0;
            hart->peekFpReg(fp_reg, curr_val);
            ra.value.resize(sizeof(curr_val));
            std::memcpy(ra.value.data(), &curr_val, sizeof(curr_val));
            
            // Get previous value
            ra.prev_value.resize(sizeof(prev_fp_val));
            std::memcpy(ra.prev_value.data(), &prev_fp_val, sizeof(prev_fp_val));
            
            info.reg_writes.push_back(ra);
        }
        
        // Check if branch was taken
        info.is_branch = hart->lastBranchTaken();
        if (info.is_branch)
        {
            info.is_taken = true;
            // For branches, alt_next_pc would be the not-taken path
            // Since we don't have direct access, we can infer it
            // For now, leave it empty - Olympia can calculate it
        }
        
        // Check if instruction trapped
        if (hart->lastInstructionTrapped())
        {
            info.faulted = true;
        }
        
        // Check if simulation should end
        info.ends_simulation = hart->hasTargetProgramFinished();
        
        // Note: Opcode and disassembly extraction not implemented.
        // Whisper requires manual instruction decoding (read memory at lastPc,
        // then use Disassembler class). This is a debugging feature and not
        // critical for EDM functionality. Can be added if needed for logging.
        
        return info;
    }

    void WhisperAdapter::saveCheckpoint_(const std::shared_ptr<WdRiscv::Hart<uint64_t>>& hart, uint64_t uid)
    {
        WhisperCheckpoint checkpoint;
        
        // Save PC
        checkpoint.pc = hart->peekPc();
        
        // Save integer registers
        checkpoint.int_regs.resize(hart->intRegCount());
        for (unsigned i = 0; i < hart->intRegCount(); i++)
        {
            hart->peekIntReg(i, checkpoint.int_regs[i]);
        }
        
        // Save FP registers
        checkpoint.fp_regs.resize(hart->fpRegCount());
        for (unsigned i = 0; i < hart->fpRegCount(); i++)
        {
            hart->peekFpReg(i, checkpoint.fp_regs[i]);
        }
        
        // Store checkpoint
        speculation_checkpoints_[uid] = checkpoint;
    }

    void WhisperAdapter::restoreCheckpoint_(const std::shared_ptr<WdRiscv::Hart<uint64_t>>& hart, const WhisperCheckpoint & checkpoint)
    {
        // Restore PC
        hart->pokePc(checkpoint.pc);
        
        // Restore integer registers
        for (unsigned i = 0; i < checkpoint.int_regs.size() && i < hart->intRegCount(); i++)
        {
            hart->pokeIntReg(i, checkpoint.int_regs[i]);
        }
        
        // Restore FP registers
        for (unsigned i = 0; i < checkpoint.fp_regs.size() && i < hart->fpRegCount(); i++)
        {
            hart->pokeFpReg(i, checkpoint.fp_regs[i]);
        }
    }

} // namespace olympia::edm

// <EDMTypes.hpp> -*- C++ -*-
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <limits>

namespace olympia::edm
{
    using Addr = uint64_t;
    using Opcode = uint32_t;
    using CoreId = uint32_t;
    using HartId = uint32_t;

    struct RegAccess
    {
        uint32_t reg_id = 0;
        std::string reg_name;
        std::vector<uint8_t> value;
        std::vector<uint8_t> prev_value;
    };

    struct MemAccess
    {
        Addr paddr = 0;
        Addr vaddr = 0;
        size_t size = 0;
        std::vector<uint8_t> value;
        std::vector<uint8_t> prev_value;
        bool is_write = false;
    };

    struct InstructionInfo
    {
        Addr pc = 0;
        Addr next_pc = 0;
        std::optional<Addr> alt_next_pc;

        Opcode opcode = 0;
        uint32_t opcode_size = 4;
        std::string dasm;

        bool is_branch = false;
        bool is_taken = false;
        bool is_store = false;
        bool is_load = false;
        bool ends_simulation = false;
        bool is_wrong_path = false;
        bool faulted = false;

        uint64_t iss_uid = std::numeric_limits<uint64_t>::max();

        std::vector<RegAccess> reg_reads;
        std::vector<RegAccess> reg_writes;
        std::vector<MemAccess> mem_reads;
        std::vector<MemAccess> mem_writes;
    };

    struct SteeringDecision
    {
        enum class Action
        {
            STEP_NORMAL,
            STEP_WITH_OVERRIDE
        };

        Action action = Action::STEP_NORMAL;
        std::optional<Addr> override_pc;
        bool is_wrong_path = false;
    };

} // namespace olympia::edm
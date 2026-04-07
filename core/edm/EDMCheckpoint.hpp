#pragma once
#include "EDMTypes.hpp"

namespace olympia::edm
{
    struct EDMCheckpoint
    {
	    CoreId core_id                   = 0;
	    HartId hart_id                   = 0;
        uint64_t olympia_inst_uid        = 0;
        uint64_t iss_uid       = std::numeric_limits<uint64_t>::max();
        Addr     branch_pc               = 0;
        Addr     correct_path_pc         = 0;
        Addr     current_path_pc        = 0;
        bool     is_wrong_path_injection = false;
    };

} 
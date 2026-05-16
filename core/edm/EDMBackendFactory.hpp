#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

#include "edm/EDMInterface.hpp"

namespace olympia::edm
{
    // Extra knobs forwarded to backend adapter constructors.
    // Keep values stringly-typed for now to avoid over-constraining
    // early bring-up.
    using BackendParams = std::map<std::string, std::string>;

    // Factory function signature used for backend registration.
    using EDMBackendFactory = std::function<std::unique_ptr<EDMInterface>(
        const std::string & workload,
        const BackendParams & params)>;

    // Register a backend by user-facing name (e.g. "whisper").
    void registerEDMBackend(const std::string & name, EDMBackendFactory factory);

    // Create backend instance from registered name.
    // This call is the single construction entry point used by generator code.
    std::unique_ptr<EDMInterface> createEDMBackend(const std::string & backend_name,
                                                   const std::string & workload,
                                                   const BackendParams & params = {});
}

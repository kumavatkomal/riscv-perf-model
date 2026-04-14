#include "EDMFactory.hpp"
#include <sparta/utils/SpartaAssert.hpp>

namespace olympia::edm
{
    std::map<std::string, EDMBackendFactory::BackendCreator>& EDMBackendFactory::getRegistry()
    {
        static std::map<std::string, BackendCreator> registry;
        return registry;
    }

    std::string& EDMBackendFactory::getDefault()
    {
        static std::string default_backend;
        return default_backend;
    }

    void EDMBackendFactory::registerBackend(const std::string& name, BackendCreator creator){
        auto& registry = getRegistry();
        auto& default_backend = getDefault();

        if(default_backend.empty()){
            default_backend = name;
        }

        registry[name] = creator;
    }

    std::string EDMBackendFactory::getDefaultBackend()
    {
        const auto& default_backend = getDefault();
        sparta_assert(!default_backend.empty(), "No EDM backend registered");
        return default_backend;
    }

    std::unique_ptr<EDMInterface> EDMBackendFactory::create(
        const std::string& workload,
        uint64_t ilimit,
        const std::map<std::string, std::string> & params,
        const std::string& db_file,
        size_t snapshot_threshold)
    {
        const auto& registry = getRegistry();
        const auto& default_backend = getDefaultBackend();
        
        auto it = registry.find(default_backend);
        sparta_assert(it != registry.end(), "Internal Error" << default_backend << " not found in registry");
        return it->second(workload, ilimit, params, db_file, snapshot_threshold);
    }
}
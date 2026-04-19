#include "EDMFactory.hpp"
#include <sparta/utils/SpartaAssert.hpp>

namespace olympia::edm
{
    std::map<std::string, EDMBackendFactory::BackendCreator>&
    EDMBackendFactory::getRegistry()
    {
        static std::map<std::string, BackendCreator> registry;
        return registry;
    }

    std::string& EDMBackendFactory::getDefault()
    {
        static std::string default_backend;
        return default_backend;
    }

    void EDMBackendFactory::registerBackend(const std::string& name, BackendCreator creator)
    {
        auto& reg = getRegistry();
        if (reg.empty())
        {
            getDefault() = name;
        }
        reg.emplace(name, std::move(creator));
    }

    std::unique_ptr<EDMInterface> EDMBackendFactory::create(
        const std::string& workload,
        uint64_t ilimit,
        const std::map<std::string, std::string>& params,
        const std::string& db_file,
        size_t snapshot_threshold)
    {
        auto& reg = getRegistry();
        const std::string& name = getDefault();

        auto it = reg.find(name);
        sparta_assert(it != reg.end(),
            "EDMBackendFactory: no backend registered under '" << name << "'");

        return it->second(workload, ilimit, params, db_file, snapshot_threshold);
    }

    std::string EDMBackendFactory::getDefaultBackend()
    {
        return getDefault();
    }
}
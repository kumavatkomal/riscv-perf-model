#include "EDMFactory.hpp"
#include <sparta/utils/SpartaAssert.hpp>

namespace olympia::edm
{
    // forward declaring the force link functions
#if defined(PEGASUS_AVAILABLE)
    void forcePegasusLink();
#else
    void forcePegasusLink() {}
#endif
    void forceWhisperLink();

    std::map<std::string, EDMBackendFactory::BackendCreator> & EDMBackendFactory::getRegistry()
    {
        static bool once = (forcePegasusLink(), forceWhisperLink(), true);
        (void)once;
        static std::map<std::string, BackendCreator> registry;
        return registry;
    }

    std::string & EDMBackendFactory::getDefault()
    {
        static std::string default_backend;
        return default_backend;
    }

    void EDMBackendFactory::registerBackend(const std::string & name, BackendCreator creator)
    {
        auto & reg = getRegistry();
        if (reg.empty())
        {
            getDefault() = name;
        }
        reg.emplace(name, std::move(creator));
    }

    std::unique_ptr<EDMInterface> EDMBackendFactory::create(const std::string & backend_name,
                                                            const std::string & config_file,
                                                            const std::string & filename,
                                                            const BackendParams & params)
    {
        auto & reg = getRegistry();
        auto it = reg.find(backend_name);
        sparta_assert(it != reg.end(),
                      "No backend with the name "
                          << backend_name
                          << "found. Ensure the backend is registered and the edm mode is enabled");
        return it->second(config_file, filename, params);
    }

    std::string EDMBackendFactory::getDefaultBackend() { return getDefault(); }
} // namespace olympia::edm

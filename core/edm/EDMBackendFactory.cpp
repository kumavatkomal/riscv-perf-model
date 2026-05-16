#include "edm/EDMBackendFactory.hpp"

#include <mutex>
#include <unordered_map>

#include "sparta/utils/SpartaAssert.hpp"
#include "edm/adapters/whisper/WhisperEDMAdapter.hpp"

namespace olympia::edm
{
    namespace
    {
        // Local static registry keeps EDM backend wiring centralized.
        auto getRegistry_() -> std::unordered_map<std::string, EDMBackendFactory> &
        {
            static std::unordered_map<std::string, EDMBackendFactory> registry;
            return registry;
        }

        // Registry lock protects registration and lookup consistency.
        auto getRegistryLock_() -> std::mutex &
        {
            static std::mutex lock;
            return lock;
        }

        // Register built-in backends once.
        //
        // We keep this lazy so startup cost stays low in trace-only runs.
        void registerBuiltinBackends_()
        {
            static bool initialized = false;
            if(initialized) {
                return;
            }

            registerEDMBackend("whisper",
                [](const std::string & workload, const BackendParams & params) {
                    return std::unique_ptr<EDMInterface>(
                        new WhisperEDMAdapter(workload, params));
                });

            initialized = true;
        }
    }

    void registerEDMBackend(const std::string & name, EDMBackendFactory factory)
    {
        std::lock_guard<std::mutex> guard(getRegistryLock_());
        auto & registry = getRegistry_();

        // Duplicate names are almost always configuration mistakes,
        // so fail fast with a clear assertion.
        sparta_assert(registry.find(name) == registry.end(),
                      "EDM backend already registered: " << name);

        registry.emplace(name, std::move(factory));
    }

    std::unique_ptr<EDMInterface> createEDMBackend(const std::string & backend_name,
                                                   const std::string & workload,
                                                   const BackendParams & params)
    {
        // Ensure built-ins are available even if caller never manually registered anything.
        registerBuiltinBackends_();

        std::lock_guard<std::mutex> guard(getRegistryLock_());
        auto & registry = getRegistry_();

        const auto it = registry.find(backend_name);
        // Keep error explicit so user gets immediate feedback on typo/unsupported backend.
        sparta_assert(it != registry.end(),
                      "Unknown EDM backend '" << backend_name << "'");

        return it->second(workload, params);
    }
}

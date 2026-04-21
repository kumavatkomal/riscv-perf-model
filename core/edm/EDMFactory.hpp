#include "EDMInterface.hpp"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <iostream>

namespace olympia::edm
{
    class EDMBackendFactory
    {
      public:
        // declaring a function for creating the backend
        // currently it is hardwired for the pegasus configuration
        // need to handle giving these all things
        // maybe have a config - for this backend where we automatically go and pick out the
        // different parameters that different backends need.
        using BackendCreator = std::function<std::unique_ptr<EDMInterface>(
            const std::string & workload, uint64_t ilimit,
            const std::map<std::string, std::string> & params, const std::string & db_file,
            size_t snapshot_threshold)>;

        static std::unique_ptr<EDMInterface>
        create(const std::string & workload, uint64_t ilimit = std::numeric_limits<uint64_t>::max(),
               const std::map<std::string, std::string> & params = {},
               const std::string & db_file = "", size_t snapsnot_threshold = 1000);

        // Registering the backend
        static void registerBackend(const std::string & name, BackendCreator creator);

        static std::string getDefaultBackend();

      private:
        static std::map<std::string, BackendCreator> & getRegistry();
        static std::string & getDefault();
    };

    /**
    The registrar - is global. This registrar uses RAII - Resource aquisition is initliazaiton. This
    means that the different backends, like pegasus and whisper - are not declared and initialize
    globally. Instead - this registrar is global and when we acquire the backend by specifying it -
    then we have the backend.
    */

    template <typename Implementation> struct BackendRegistrar
    {
        BackendRegistrar(const std::string & name)
        {

            std::cout << "registering the backend :" << name << std::endl;
            EDMBackendFactory::registerBackend(
                name,
                [](const std::string & w, uint64_t il, const std::map<std::string, std::string> & p,
                   const std::string & db, size_t snap)
                { return std::make_unique<Implementation>(w, il, p, db, snap); });
        }
    };
} // namespace olympia::edm

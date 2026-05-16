// <SimulationConfiguration.hpp> -*- C++ -*-

#pragma once

#include <cinttypes>
#include <vector>
#include <string>

#include "sparta/simulation/Parameter.hpp"
#include "sparta/simulation/TreeNode.hpp"
#include "sparta/simulation/ResourceTreeNode.hpp"
#include "sparta/simulation/TreeNodeExtensions.hpp"

namespace olympia
{
    /**
     * Create a CoreExtension object that will contain the
     * trace file and the instruction limit.  This object will be
     * created when the extension is specified on the command line OR
     * from within an architecture/configuration file.
     *
     * Extensions are like preferences for the model.  These are
     * Parameters, but not tied to a sparta::Unit/Resource.  They do
     * reside in the tree and can be "grabbed" by any unit, anywhere.
     *
     * The extension is created in OlympiaSim
     */
    class SimulationConfiguration : public sparta::ExtensionsParamsOnly
    {
    public:
        SimulationConfiguration() : sparta::ExtensionsParamsOnly() {}
        virtual ~SimulationConfiguration() {}

    private:
        void postCreate() override {
            sparta::ParameterSet * ps = getParameters();
            if(nullptr == ps->getParameter("workload", false)) {
                workload_param_.reset(new sparta::Parameter<std::string>(
                                          "workload", "",
                                          "Workload to run", ps));
            }

            if(nullptr == ps->getParameter("edm_backend", false)) {
                edm_backend_param_.reset(new sparta::Parameter<std::string>(
                                             "edm_backend", "",
                                             "EDM backend selector (empty for trace/json mode)", ps));
            }

            if(nullptr == ps->getParameter("edm_backend_config", false)) {
                edm_backend_config_param_.reset(new sparta::Parameter<std::string>(
                                                    "edm_backend_config", "",
                                                    "Optional EDM backend config file path", ps));
            }

            if(nullptr == ps->getParameter("edm_whisper_bin", false)) {
                edm_whisper_bin_param_.reset(new sparta::Parameter<std::string>(
                                                 "edm_whisper_bin", "",
                                                 "Override Whisper binary path", ps));
            }

            if(nullptr == ps->getParameter("edm_whisper_args", false)) {
                edm_whisper_args_param_.reset(new sparta::Parameter<std::string>(
                                                  "edm_whisper_args", "",
                                                  "Extra Whisper CLI arguments", ps));
            }

            if(nullptr == ps->getParameter("edm_whisper_keep_csv", false)) {
                edm_whisper_keep_csv_param_.reset(new sparta::Parameter<std::string>(
                                                      "edm_whisper_keep_csv", "",
                                                      "Keep Whisper CSV trace (true/false/1/0)", ps));
            }
        }

        std::unique_ptr<sparta::Parameter<std::string>> workload_param_;
        std::unique_ptr<sparta::Parameter<std::string>> edm_backend_param_;
        std::unique_ptr<sparta::Parameter<std::string>> edm_backend_config_param_;
        std::unique_ptr<sparta::Parameter<std::string>> edm_whisper_bin_param_;
        std::unique_ptr<sparta::Parameter<std::string>> edm_whisper_args_param_;
        std::unique_ptr<sparta::Parameter<std::string>> edm_whisper_keep_csv_param_;

    };
}

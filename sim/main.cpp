// <main.cpp> -*- C++ -*-


#include <iostream>

#include "OlympiaSim.hpp" // Core model example simulator

#include "sparta/app/CommandLineSimulator.hpp"
#include "sparta/app/MultiDetailOptions.hpp"
#include "sparta/sparta.hpp"
#include "git_version.h"

// User-friendly usage that correspond with sparta::app::CommandLineSimulator
// options
const char USAGE[] =
    "Usage:\n"
    "    [-i insts] [-r RUNTIME] [--show-tree] [--show-dag]\n"
    "    [-p PATTERN VAL] [-c FILENAME]\n"
    "    [-l PATTERN CATEGORY DEST]\n"
    "    [--edm-backend BACKEND]\n"
    "    [-h,--help] <workload [stf trace, JSON, or EDM input]>\n"
    "\n";

constexpr char VERSION_VARNAME[] = "version,v"; //!< Name of option to show version

int main(int argc, char **argv)
{
    uint64_t ilimit = 0;
    uint32_t num_cores = 1;
    std::string workload;
    std::string edm_backend;
    std::string edm_backend_config;
    std::string edm_whisper_bin;
    std::string edm_whisper_args;
    std::string edm_whisper_keep_csv;
    const char * WORKLOAD = "workload";

    sparta::app::DefaultValues DEFAULTS;
    DEFAULTS.auto_summary_default = "off";
    DEFAULTS.arch_arg_default = "small_core";
    DEFAULTS.arch_search_dirs = {"arches"}; // Where --arch will be resolved by default

    const std::string olympia_version = " " + std::string(OLYMPIA_VERSION);
    const std::string reproduction_info = "Git SHA: " + std::string(GIT_COMMIT_HASH);
    sparta::SimulationInfo::getInstance() = sparta::SimulationInfo("Olympia RISC-V Perf Model ",
                                                                    argc, 
                                                                    argv, 
                                                                    olympia_version.c_str(),
                                                                    reproduction_info.c_str(), {});
    const bool show_field_names = true;
    sparta::SimulationInfo::getInstance().write(std::cout, "# ", "\n", show_field_names);
    std::cout << "# Sparta Version: " << sparta::SimulationInfo::sparta_version << std::endl;

    // try/catch block to ensure proper destruction of the cls/sim classes in
    // the event of an error
    try{
        // Helper class for parsing command line arguments, setting up the
        // simulator, and running the simulator. All of the things done by this
        // classs can be done manually if desired. Use the source for the
        // CommandLineSimulator class as a starting point
        sparta::app::CommandLineSimulator cls(USAGE, DEFAULTS);
        auto& app_opts = cls.getApplicationOptions();
        app_opts.add_options()
            (VERSION_VARNAME,
             "produce version message",
             "produce version message") // Brief
            ("instruction-limit,i",
             sparta::app::named_value<uint64_t>("LIMIT", &ilimit)->default_value(ilimit),
             "Limit the simulation to retiring a specific number of instructions. 0 (default) "
             "means no limit. If -r is also specified, the first limit reached ends the simulation",
             "End simulation after a number of instructions. Note that if set to 0, this may be "
             "overridden by a node parameter within the simulator")
            ("num-cores",
             sparta::app::named_value<uint32_t>("CORES", &num_cores)->default_value(1),
             "The number of cores in simulation", "The number of cores in simulation")
            ("show-factories",
             "Show the registered factories")
            ("edm-backend",
             sparta::app::named_value<std::string>("BACKEND", &edm_backend)->default_value(""),
             "Select EDM backend (e.g., pegasus, whisper). Empty means trace/json mode.",
             "Select EDM backend")
            ("edm-backend-config",
             sparta::app::named_value<std::string>("FILE", &edm_backend_config)->default_value(""),
             "Optional EDM backend config file path",
             "EDM backend config")
            ("edm-whisper-bin",
             sparta::app::named_value<std::string>("PATH", &edm_whisper_bin)->default_value(""),
             "Override Whisper binary path",
             "Whisper binary path")
            ("edm-whisper-args",
             sparta::app::named_value<std::string>("ARGS", &edm_whisper_args)->default_value(""),
             "Extra Whisper CLI arguments",
             "Whisper args")
            ("edm-whisper-keep-csv",
             sparta::app::named_value<std::string>("BOOL", &edm_whisper_keep_csv)->default_value(""),
             "Keep Whisper CSV trace (true/false/1/0)",
             "Whisper CSV retention")
            (WORKLOAD,
             sparta::app::named_value<std::string>(WORKLOAD, &workload),
             "Specifies the instruction workload (trace, JSON, or EDM input)");

        // Add any positional command-line options
        po::positional_options_description& pos_opts = cls.getPositionalOptions();
        pos_opts.add(WORKLOAD, -1);

        // Parse command line options and configure simulator
        int err_code = 0;
        if(!cls.parse(argc, argv, err_code)){
            return err_code; // Any errors already printed to cerr
        }

        auto& vm = cls.getVariablesMap();
        if(vm.count("auto-summary") == 0) {
            std::cout << "olympia: Auto-summary reports are disabled in Olympia.  To enable, use '--auto-summary on'"
                      << std::endl;
        }

        bool show_factories = false;
        if(vm.count("show-factories") != 0) {
            show_factories = true;
        }

        if(workload.empty() && (0 == vm.count("no-run"))) {
            std::cerr << "ERROR: Missing a workload to run.  Can be a trace or JSON file" << std::endl;
            std::cerr << USAGE;
            return -1;
        }

        // Create the simulator
        sparta::Scheduler scheduler;
        OlympiaSim sim(scheduler,
                       num_cores, // cores
                       workload,
                       edm_backend,
                       edm_backend_config,
                       edm_whisper_bin,
                       edm_whisper_args,
                       edm_whisper_keep_csv,
                       ilimit,
                       show_factories); // run for ilimit instructions

        cls.populateSimulation(&sim);

        cls.runSimulator(&sim);

        cls.postProcess(&sim);

    }catch(...){
        // Could still handle or log the exception here
        throw;
    }
    return 0;
}

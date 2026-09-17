#include <iostream>
#include <cstdlib>
#include <csignal>
#include "aemlpc/config/Config.hpp"
#include "aemlpc/object/ObjectManager.hpp"
#include "aemlpc/vm/VM.hpp"
#include "aemlpc/net/Server.hpp"
#include "aemlpc/scheduler/Scheduler.hpp"
#include "aemlpc/efun/EfunTable.hpp"
#include "aemlpc/dialect/DialectSelect.hpp"
#include "aemlpc/dialect/MasterUidBoot.hpp"
#include "aemlpc/dialect/InaugurateMasterBoot.hpp"

namespace {
void handleSignal(int) {
    aemlpc::Scheduler::requestShutdown();
}
}

// Ignore SIGPIPE: a peer closing mid-write must return EPIPE, not
// kill the process (default disposition; confirmed live as exit 141).

int main(int argc, char** argv) {
    // No default config path: every real invocation already passes one.
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <config-path> [max-iterations]\n";
        return 1;
    }
    std::string configPath = argv[1];

    int maxIterations = 0;
    if (argc > 2) {
        maxIterations = std::atoi(argv[2]);
    } else if (const char* envVal = std::getenv("AEMLPC_MAX_ITERATIONS")) {
        maxIterations = std::atoi(envVal);
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    std::signal(SIGPIPE, SIG_IGN);

    aemlpc::Config config;
    if (!config.loadFromFile(configPath)) {
        std::cerr << "Failed to load config: " << configPath << "\n";
        return 1;
    }

    aemlpc::registerCoreEfuns();

    aemlpc::ObjectManager objectManager(config);
    aemlpc::VM vm(objectManager, config);
    objectManager.setVM(&vm);
    // ObjectManager cannot link EfunTable directly; this lambda can.
    objectManager.setEfunExistsChecker([](const std::string& name) {
        return aemlpc::EfunTable::instance().exists(name);
    });

    aemlpc::Scheduler scheduler(vm);
    vm.setScheduler(&scheduler);
    aemlpc::Server server(config, vm, objectManager, scheduler);

    std::cout << "aemlpc booting...\n";
    std::cout << "  mudlib_root = " << config.mudlibRoot() << "\n";
    std::cout << "  master_file = " << config.masterFile() << "\n";
    std::cout << "  port        = " << config.port() << "\n";

    // FluffOS main.c:311-319: simul_efun first, then master. LDMud loads
    // simul_efun lazily (assert_simul_efun_object); eager load is still
    // compatible. Master create() may call simul_efuns (file_exists).
    if (!config.simulEfunFile().empty()) {
        if (objectManager.loadSimulEfunObject()) {
            std::cout << "Simul_efun object loaded: " << config.simulEfunFile() << "\n";
        } else {
            std::cerr << "Warning: failed to load simul_efun object "
                       << config.simulEfunFile() << " (continuing without it)\n";
        }
    }

    if (!objectManager.loadMasterObject()) {
        std::cerr << "Failed to load master object: " << config.masterFile() << "\n";
        return 1;
    }

    std::cout << "Driver booted. Master object loaded: " << config.masterFile() << "\n";

    // Per-dialect boot-time master UID query (MasterUidBoot.hpp).
    auto bootApi = aemlpc::makeBootApiForConfig(config);
    if (auto uid = aemlpc::queryMasterUid(vm, *bootApi)) {
        std::cout << "  master " << bootApi->masterUidApply() << "() = \"" << *uid << "\"\n";
    } else {
        std::cout << "  master does not define " << bootApi->masterUidApply() << "()\n";
    }

    // LDMud inaugurate_master(0) (main.c:661-663). No-op under FluffOS/DGD.
    if (auto inaugurateApply = bootApi->inaugurateMasterApply()) {
        std::cout << "  master " << *inaugurateApply << "(0) ...\n";
    }
    aemlpc::applyInaugurateMaster(vm, *bootApi);

    if (!server.listen()) {
        std::cerr << "Failed to start network listener on port " << config.port() << "\n";
        return 1;
    }

    std::cout << "Ready for connections. Try: telnet localhost " << config.port() << "\n";
    if (maxIterations > 0) {
        std::cout << "(test mode: will exit after " << maxIterations << " poll iterations)\n";
    } else {
        std::cout << "(press Ctrl-C to stop)\n";
    }

    scheduler.run(server, maxIterations);

    std::cout << "aemlpc shutting down.\n";
    return 0;
}

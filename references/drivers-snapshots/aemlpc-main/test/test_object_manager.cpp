#include "aemlpc/config/Config.hpp"
#include "aemlpc/object/ObjectManager.hpp"

#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace {

struct ObjectHarness {
    std::string tempDir;
    aemlpc::Config config;
    aemlpc::ObjectManager objects;

    ObjectHarness()
        : objects(config) {
        char dirTemplate[] = "/tmp/aemlpc_object_test_XXXXXX";
        char* created = mkdtemp(dirTemplate);
        assert(created != nullptr);
        tempDir = created;

        std::string cfgPath = tempDir + "/driver.cfg";
        std::ofstream cfg(cfgPath);
        cfg << "mudlib_root: " << tempDir << "\n";
        cfg << "master_file: /unused\n";
        cfg << "include_dir: " << tempDir << "\n";
        cfg << "mud_name: Nightmare Residuum\n";
        cfg.close();
        assert(config.loadFromFile(cfgPath));
    }

    void writeFile(const std::string& relPath, const std::string& contents) {
        std::ofstream f(tempDir + relPath);
        f << contents;
    }
};

void testCompileWithSpacedMudName() {
    ObjectHarness harness;
    harness.writeFile("/probe.c", "int ping() { return 1; }\n");

    auto obj = harness.objects.loadObject("/probe");
    assert(obj != nullptr);

    std::cout << "testCompileWithSpacedMudName OK\n";
}

} // namespace

int main() {
    testCompileWithSpacedMudName();
    return 0;
}

#include "amlp/dialect/InaugurateMasterBoot.hpp"

#include <iostream>

#include "amlp/dialect/BootApi.hpp"
#include "amlp/vm/VM.hpp"

namespace amlp {

void applyInaugurateMaster(VM& vm, const BootApi& bootApi) {
    auto applyName = bootApi.inaugurateMasterApply();
    if (!applyName) return;
    try {
        // arg=0: "the mud just started, this is the first master of
        // all" (doc/master/inaugurate_master) -- the master-reload/
        // reactivation arg values (1/2/3) are a separate, still-open
        // concern, not part of this boot-sequence slice.
        vm.applyMaster(*applyName, {Value(static_cast<int64_t>(0))});
    } catch (const std::exception& e) {
        std::cerr << "[dialect] " << *applyName << "(0) failed: " << e.what() << "\n";
    }
}

} // namespace amlp

#include "aemlpc/dialect/MasterUidBoot.hpp"

#include <iostream>
#include <variant>

#include "aemlpc/dialect/BootApi.hpp"
#include "aemlpc/vm/VM.hpp"

namespace aemlpc {

std::optional<std::string> queryMasterUid(VM& vm, const BootApi& bootApi) {
    const std::string applyName = bootApi.masterUidApply();
    Value result;
    try {
        result = vm.applyMaster(applyName, {});
    } catch (const std::exception& e) {
        std::cerr << "[dialect] " << applyName << "() failed: " << e.what() << "\n";
        return std::nullopt;
    }
    if (auto* s = std::get_if<std::string>(&result.data)) {
        return *s;
    }
    return std::nullopt;
}

} // namespace aemlpc

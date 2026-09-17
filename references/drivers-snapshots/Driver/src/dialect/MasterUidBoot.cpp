#include "amlp/dialect/MasterUidBoot.hpp"

#include <iostream>
#include <variant>

#include "amlp/dialect/BootApi.hpp"
#include "amlp/vm/VM.hpp"

namespace amlp {

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

} // namespace amlp

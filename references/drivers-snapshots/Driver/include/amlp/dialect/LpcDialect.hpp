#pragma once
#include <string>

namespace amlp {

enum class LpcDialect {
    FluffOS,   // MudOS/FluffOS (: :) LPC - current default
    LdMud,     // Amylaar/LDMud #' lambda() LPC
    DGD,       // Dworkin's Generic Driver nil/atomic/rlimits LPC
};

const char* dialectName(LpcDialect d);              // "fluffos" / "ldmud" / "dgd"
LpcDialect dialectFromString(const std::string& s);  // throws std::invalid_argument on unknown

} // namespace amlp

#include "amlp/dialect/LpcDialect.hpp"

#include <stdexcept>

namespace amlp {

const char* dialectName(LpcDialect d) {
    switch (d) {
        case LpcDialect::FluffOS: return "fluffos";
        case LpcDialect::LdMud:   return "ldmud";
        case LpcDialect::DGD:     return "dgd";
    }
    throw std::invalid_argument("dialectName(): unknown LpcDialect value");
}

LpcDialect dialectFromString(const std::string& s) {
    if (s == "fluffos") return LpcDialect::FluffOS;
    if (s == "ldmud") return LpcDialect::LdMud;
    if (s == "dgd") return LpcDialect::DGD;
    throw std::invalid_argument("dialectFromString(): unknown dialect name '" + s + "'");
}

} // namespace amlp

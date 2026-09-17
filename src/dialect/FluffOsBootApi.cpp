#include "kjdmud/dialect/FluffOsBootApi.hpp"

#include "kjdmud/config/Config.hpp"

namespace kjdmud {

std::string FluffOsBootApi::masterFile() const {
    return config_.masterFile();
}

std::optional<std::string> FluffOsBootApi::simulEfunFile() const {
    const std::string& file = config_.simulEfunFile();
    if (file.empty()) return std::nullopt;
    return file;
}

} // namespace kjdmud

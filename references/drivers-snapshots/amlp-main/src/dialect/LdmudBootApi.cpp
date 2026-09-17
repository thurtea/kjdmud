#include "amlp/dialect/LdmudBootApi.hpp"

#include "amlp/config/Config.hpp"

namespace amlp {

std::string LdmudBootApi::masterFile() const {
    return config_.masterFile();
}

std::optional<std::string> LdmudBootApi::simulEfunFile() const {
    const std::string& file = config_.simulEfunFile();
    if (file.empty()) return std::nullopt;
    return file;
}

} // namespace amlp

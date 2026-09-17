#include "amlp/dialect/DialectSelect.hpp"

#include "amlp/config/Config.hpp"
#include "amlp/core/Errors.hpp"
#include "amlp/dialect/FluffOsBootApi.hpp"
#include "amlp/dialect/LdmudBootApi.hpp"
#include "amlp/dialect/LpcDialect.hpp"

namespace amlp {

std::unique_ptr<BootApi> makeBootApiForConfig(const Config& config) {
    switch (dialectFromString(config.dialect())) {
        case LpcDialect::FluffOS:
            return std::make_unique<FluffOsBootApi>(config);
        case LpcDialect::LdMud:
            return std::make_unique<LdmudBootApi>(config);
        case LpcDialect::DGD:
            throw NotImplementedError("dgd dialect (DgdBootApi does not exist yet)");
    }
    throw NotImplementedError("makeBootApiForConfig(): unknown LpcDialect value");
}

} // namespace amlp

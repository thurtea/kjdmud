#include "aemlpc/dialect/DialectSelect.hpp"

#include "aemlpc/config/Config.hpp"
#include "aemlpc/core/Errors.hpp"
#include "aemlpc/dialect/FluffOsBootApi.hpp"
#include "aemlpc/dialect/LdmudBootApi.hpp"
#include "aemlpc/dialect/LpcDialect.hpp"

namespace aemlpc {

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

} // namespace aemlpc

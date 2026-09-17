#include "kjdmud/dialect/DialectSelect.hpp"

#include "kjdmud/config/Config.hpp"
#include "kjdmud/core/Errors.hpp"
#include "kjdmud/dialect/FluffOsBootApi.hpp"
#include "kjdmud/dialect/LdmudBootApi.hpp"
#include "kjdmud/dialect/LpcDialect.hpp"

namespace kjdmud {

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

} // namespace kjdmud

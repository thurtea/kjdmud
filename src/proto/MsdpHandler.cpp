#include "kjdmud/proto/MsdpHandler.hpp"
#include "kjdmud/net/Connection.hpp"

namespace kjdmud {

void MsdpHandler::send(Connection& conn, const std::string& varName,
                        const std::string& value) {
    conn.sendMsdp(varName, value);
}

} // namespace kjdmud

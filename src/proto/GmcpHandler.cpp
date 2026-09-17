#include "kjdmud/proto/GmcpHandler.hpp"
#include "kjdmud/net/Connection.hpp"

namespace kjdmud {

void GmcpHandler::send(Connection& conn, const std::string& package) {
    conn.sendGmcp(package);
}

} // namespace kjdmud

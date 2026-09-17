#include "aemlpc/proto/GmcpHandler.hpp"
#include "aemlpc/net/Connection.hpp"

namespace aemlpc {

void GmcpHandler::send(Connection& conn, const std::string& package) {
    conn.sendGmcp(package);
}

} // namespace aemlpc

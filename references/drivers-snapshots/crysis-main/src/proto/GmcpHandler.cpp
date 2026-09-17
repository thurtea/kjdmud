#include "amlp/proto/GmcpHandler.hpp"
#include "amlp/net/Connection.hpp"

namespace amlp {

void GmcpHandler::send(Connection& conn, const std::string& package) {
    conn.sendGmcp(package);
}

} // namespace amlp

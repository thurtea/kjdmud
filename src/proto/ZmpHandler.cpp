#include "kjdmud/proto/ZmpHandler.hpp"
#include "kjdmud/net/Connection.hpp"

namespace kjdmud {

void ZmpHandler::send(Connection& conn, const std::string& command,
                       const std::vector<std::string>& args) {
    conn.sendZmp(command, args);
}

} // namespace kjdmud

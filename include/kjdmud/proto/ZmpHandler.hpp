#pragma once
#include <string>
#include <vector>

namespace kjdmud {

class Connection;

// v1 ZMP only: verified real efun names/signatures (FluffOS names
// has_zmp/send_zmp/apply zmp_command) live in NetEfuns.cpp. This helper
// wraps the IAC SB 93 payload, same shape as GmcpHandler/MsdpHandler.
class ZmpHandler {
public:
    static void send(Connection& conn, const std::string& command,
                      const std::vector<std::string>& args);
};

} // namespace kjdmud

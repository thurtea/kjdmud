#pragma once
#include <string>

namespace kjdmud {

class Connection;

// v1 MSDP only (single scalar MSDP_VAR/MSDP_VAL pairs; see
// Connection.hpp's own sendMsdp() comment for the scope note). FluffOS
// names (has_msdp / send_msdp_variable / object msdp(var, val)) live in
// NetEfuns.cpp. This helper wraps the IAC SB 69 payload, same shape as
// GmcpHandler.
class MsdpHandler {
public:
    static void send(Connection& conn, const std::string& varName,
                      const std::string& value);
};

} // namespace kjdmud

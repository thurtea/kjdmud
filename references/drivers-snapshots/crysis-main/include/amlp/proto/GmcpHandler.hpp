#pragma once
#include <string>

namespace amlp {

class Connection;

// v1 GMCP only. FluffOS names (has_gmcp / send_gmcp / object gmcp())
// live in NetEfuns.cpp. This helper wraps the IAC SB 201 payload.
class GmcpHandler {
public:
    static void send(Connection& conn, const std::string& package);
};

} // namespace amlp

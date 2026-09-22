#pragma once
#include <string>
#include <vector>

namespace kjdmud {

class Connection;

// v1 ZMP only: verified real efun names/signatures (FluffOS names
// has_zmp/send_zmp/apply zmp_command) live in NetEfuns.cpp. This helper
// wraps the IAC SB 93 payload, same shape as GmcpHandler/MsdpHandler.
//
// Mudlib side: an interactive object receives an incoming ZMP message
// by defining "void zmp_command(string cmd, mixed *args)" (the real
// LPC-mapped apply name, src/vm/internal/applies: "ZMP:zmp_command").
// No command_giver argument - the apply runs on the interactive object
// itself, same as terminal_type()/window_size() already do. Send one
// back with send_zmp(string command, string *args) (real core.spec
// signature: "void send_zmp(string, string *);"), which reads
// command_giver, not current_object. Usage example and framing notes:
// docs/dev/PROTOCOLS.md.
class ZmpHandler {
public:
    static void send(Connection& conn, const std::string& command,
                      const std::vector<std::string>& args);
};

} // namespace kjdmud

#pragma once
#include <string>

namespace kjdmud {

class Connection;

// v1 MXP only: three text-wrap helpers, no tag-locking/security-mode
// machinery (see Connection.hpp's own mxpEnabled() comment for the wire
// scope). Unlike GmcpHandler/MsdpHandler, this class's own methods are
// the real logic, not a thin wrapper over a Connection method: wrapping
// text is a pure string transform with no protocol state to own, so
// there is nothing for Connection itself to do here beyond exposing the
// read-only mxpEnabled() flag these methods already consult.
class MxpHandler {
public:
    // Wraps text in the given MXP element if the connection has MXP
    // enabled; returns the text unmodified otherwise, so mudlib code can
    // call these unconditionally without its own has_mxp() branch.
    static std::string bold(Connection& conn, const std::string& text);
    static std::string color(Connection& conn, const std::string& text,
                              const std::string& fg, const std::string& bg);
    static std::string link(Connection& conn, const std::string& text,
                             const std::string& command);
};

} // namespace kjdmud

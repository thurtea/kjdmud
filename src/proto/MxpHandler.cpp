#include "kjdmud/proto/MxpHandler.hpp"
#include "kjdmud/net/Connection.hpp"

namespace kjdmud {

std::string MxpHandler::bold(Connection& conn, const std::string& text) {
    if (!conn.mxpEnabled()) return text;
    return "<B>" + text + "</B>";
}

std::string MxpHandler::color(Connection& conn, const std::string& text,
                               const std::string& fg, const std::string& bg) {
    if (!conn.mxpEnabled()) return text;
    return "<COLOR FORE=" + fg + " BACK=" + bg + ">" + text + "</COLOR>";
}

std::string MxpHandler::link(Connection& conn, const std::string& text,
                              const std::string& command) {
    if (!conn.mxpEnabled()) return text;
    return "<SEND \"" + command + "\">" + text + "</SEND>";
}

} // namespace kjdmud

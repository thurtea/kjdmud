#include "amlp/apply/ApplyTable.hpp"

namespace amlp {

const std::unordered_set<std::string>& ApplyTable::known() {
    static const std::unordered_set<std::string> names = {
        "create", "init", "clean_up", "heart_beat",
        // Real FluffOS has no "disconnect" apply at all (confirmed by
        // grepping applies.h directly) -- the actual link-death apply is
        // "net_dead" (APPLY_NET_DEAD), now genuinely fired, see
        // Server::fireNetDeadIfLinkDead(). This entry used to say
        // "disconnect", a name that never existed in the reference
        // driver and was never wired to anything here either.
        "connect", "logon", "net_dead",
        "id", "short", "long",
        "catch_tell", "receive_message", "process_input",
        "compile_object", "valid_read", "valid_write",
        "valid_socket", "get_root_uid", "epilog", "flag"
    };
    return names;
}

bool ApplyTable::isKnownApply(const std::string& name) {
    return known().count(name) > 0;
}

} // namespace amlp

#include "kjdmud/net/SnoopRelay.hpp"
#include "kjdmud/net/Connection.hpp"
#include "kjdmud/object/LpcObject.hpp"
#include "kjdmud/vm/VM.hpp"

namespace kjdmud {

void deliverToConnection(VM& vm, Connection* conn, const std::string& text) {
    if (!conn) return;
    conn->send(text);

    auto victim = conn->boundObject();
    if (!victim) return;
    auto snooper = victim->snoopedBy().lock();
    if (!snooper) return;

    vm.callFunction(snooper, "receive_snoop", {Value(text)});
}

} // namespace kjdmud

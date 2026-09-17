#include "aemlpc/net/SnoopRelay.hpp"
#include "aemlpc/net/Connection.hpp"
#include "aemlpc/object/LpcObject.hpp"
#include "aemlpc/vm/VM.hpp"

namespace aemlpc {

void deliverToConnection(VM& vm, Connection* conn, const std::string& text) {
    if (!conn) return;
    conn->send(text);

    auto victim = conn->boundObject();
    if (!victim) return;
    auto snooper = victim->snoopedBy().lock();
    if (!snooper) return;

    vm.callFunction(snooper, "receive_snoop", {Value(text)});
}

} // namespace aemlpc

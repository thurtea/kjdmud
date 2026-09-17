#include "amlp/net/SnoopRelay.hpp"
#include "amlp/net/Connection.hpp"
#include "amlp/object/LpcObject.hpp"
#include "amlp/vm/VM.hpp"

namespace amlp {

void deliverToConnection(VM& vm, Connection* conn, const std::string& text) {
    if (!conn) return;
    conn->send(text);

    auto victim = conn->boundObject();
    if (!victim) return;
    auto snooper = victim->snoopedBy().lock();
    if (!snooper) return;

    vm.callFunction(snooper, "receive_snoop", {Value(text)});
}

} // namespace amlp

#include "amlp/efun/EfunTable.hpp"
#include "amlp/vm/VM.hpp"
#include "amlp/vm/Value.hpp"
#include "amlp/config/Config.hpp"
#include "amlp/net/Connection.hpp"
#include "amlp/net/InteractiveRegistry.hpp"
#include "amlp/object/LpcObject.hpp"
#include "amlp/proto/GmcpHandler.hpp"
#include "amlp/core/Errors.hpp"
#include <vector>

namespace amlp {
namespace {

Connection* connectionFor(VM& vm, std::vector<Value>& args) {
    std::shared_ptr<LpcObject> ob;
    if (!args.empty() && std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
        ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
    } else {
        ob = vm.commandGiver();
        if (!ob) ob = vm.currentObject();
    }
    if (!ob) return nullptr;
    return InteractiveRegistry::find(ob);
}

} // namespace

void registerNetEfuns() {
    auto& t = EfunTable::instance();

    // current FluffOS sys.cc: ({ slot, "telnet"|"websocket", port, tls })
    t.registerEfun("sys_network_ports", [](VM& vm, std::vector<Value>&) -> Value {
        auto result = std::make_shared<Array>();
        std::vector<ListenPort> ports = vm.config().listenPorts();
        if (ports.empty()) {
            ListenPort fallback;
            fallback.port = vm.config().port();
            ports.push_back(fallback);
        }
        for (const auto& spec : ports) {
            auto entry = std::make_shared<Array>();
            entry->items.emplace_back(static_cast<int64_t>(spec.slot));
            entry->items.emplace_back(std::string(
                spec.kind == ListenKind::WebSocket ? "websocket" : "telnet"));
            entry->items.emplace_back(static_cast<int64_t>(spec.port));
            entry->items.emplace_back(static_cast<int64_t>(spec.tls ? 1 : 0));
            result->items.emplace_back(entry);
        }
        return Value(result);
    });

    t.registerEfun("query_connection_tls", [](VM& vm, std::vector<Value>& args) -> Value {
        Connection* conn = connectionFor(vm, args);
        return Value(static_cast<int64_t>(conn && conn->isTls() ? 1 : 0));
    });

    t.registerEfun("set_encoding", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("set_encoding: expected a string argument");
        }
        auto ob = vm.commandGiver();
        if (!ob) ob = vm.currentObject();
        Connection* conn = ob ? InteractiveRegistry::find(ob) : nullptr;
        if (!conn) return Value(int64_t{0});
        conn->setEncoding(std::get<std::string>(args[0].data));
        return Value(int64_t{1});
    });

    t.registerEfun("query_encoding", [](VM& vm, std::vector<Value>& args) -> Value {
        Connection* conn = connectionFor(vm, args);
        if (!conn) return Value{};
        return Value(conn->encoding());
    });

    t.registerEfun("has_gmcp", [](VM& vm, std::vector<Value>& args) -> Value {
        Connection* conn = connectionFor(vm, args);
        return Value(static_cast<int64_t>(conn && conn->gmcpEnabled() ? 1 : 0));
    });

    t.registerEfun("send_gmcp", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("send_gmcp: expected a string argument");
        }
        auto ob = vm.commandGiver();
        if (!ob) ob = vm.currentObject();
        Connection* conn = ob ? InteractiveRegistry::find(ob) : nullptr;
        if (!conn) return Value{};
        GmcpHandler::send(*conn, std::get<std::string>(args[0].data));
        return Value{};
    });
}

} // namespace amlp

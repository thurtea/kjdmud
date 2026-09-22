#include "kjdmud/efun/EfunTable.hpp"
#include "kjdmud/vm/VM.hpp"
#include "kjdmud/vm/Value.hpp"
#include "kjdmud/config/Config.hpp"
#include "kjdmud/net/Connection.hpp"
#include "kjdmud/net/InteractiveRegistry.hpp"
#include "kjdmud/object/LpcObject.hpp"
#include "kjdmud/proto/GmcpHandler.hpp"
#include "kjdmud/proto/MsdpHandler.hpp"
#include "kjdmud/proto/MxpHandler.hpp"
#include "kjdmud/core/Errors.hpp"
#include <vector>

namespace kjdmud {
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

// Unlike connectionFor() above (an optional object arg falling back to
// the current command_giver, the shape has_gmcp/has_msdp/has_mxp use),
// the MXP wrap efuns format text for a specific target player who is
// not necessarily whoever is currently executing - the natural case is
// building an already-wrapped string once, before it goes out via
// write()/tell_object() to that player. So the object argument is
// required, not optional, matching src/proto/instruct.md's own
// mxp_tag(object player, ...) sketch shape.
Connection* connectionForRequiredObjectArg(std::vector<Value>& args, const char* efunName) {
    if (args.empty() || !std::holds_alternative<std::shared_ptr<LpcObject>>(args[0].data)) {
        throw LpcRuntimeError(std::string(efunName) + ": expected an object as the first argument");
    }
    auto ob = std::get<std::shared_ptr<LpcObject>>(args[0].data);
    return ob ? InteractiveRegistry::find(ob) : nullptr;
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

    t.registerEfun("has_msdp", [](VM& vm, std::vector<Value>& args) -> Value {
        Connection* conn = connectionFor(vm, args);
        return Value(static_cast<int64_t>(conn && conn->msdpEnabled() ? 1 : 0));
    });

    t.registerEfun("send_msdp", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("send_msdp: expected two string arguments (var, value)");
        }
        auto ob = vm.commandGiver();
        if (!ob) ob = vm.currentObject();
        Connection* conn = ob ? InteractiveRegistry::find(ob) : nullptr;
        if (!conn) return Value{};
        MsdpHandler::send(*conn, std::get<std::string>(args[0].data),
                           std::get<std::string>(args[1].data));
        return Value{};
    });

    t.registerEfun("has_mxp", [](VM& vm, std::vector<Value>& args) -> Value {
        Connection* conn = connectionFor(vm, args);
        return Value(static_cast<int64_t>(conn && conn->mxpEnabled() ? 1 : 0));
    });

    // string mxp_bold(object ob, string text)
    t.registerEfun("mxp_bold", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("mxp_bold: expected (object, string)");
        }
        Connection* conn = connectionForRequiredObjectArg(args, "mxp_bold");
        const std::string& text = std::get<std::string>(args[1].data);
        if (!conn) return Value(text);
        return Value(MxpHandler::bold(*conn, text));
    });

    // string mxp_color(object ob, string text, string fg, string bg)
    t.registerEfun("mxp_color", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 4 || !std::holds_alternative<std::string>(args[1].data) ||
            !std::holds_alternative<std::string>(args[2].data) ||
            !std::holds_alternative<std::string>(args[3].data)) {
            throw LpcRuntimeError("mxp_color: expected (object, string, string, string)");
        }
        Connection* conn = connectionForRequiredObjectArg(args, "mxp_color");
        const std::string& text = std::get<std::string>(args[1].data);
        if (!conn) return Value(text);
        return Value(MxpHandler::color(*conn, text,
            std::get<std::string>(args[2].data), std::get<std::string>(args[3].data)));
    });

    // string mxp_link(object ob, string text, string command)
    t.registerEfun("mxp_link", [](VM&, std::vector<Value>& args) -> Value {
        if (args.size() < 3 || !std::holds_alternative<std::string>(args[1].data) ||
            !std::holds_alternative<std::string>(args[2].data)) {
            throw LpcRuntimeError("mxp_link: expected (object, string, string)");
        }
        Connection* conn = connectionForRequiredObjectArg(args, "mxp_link");
        const std::string& text = std::get<std::string>(args[1].data);
        if (!conn) return Value(text);
        return Value(MxpHandler::link(*conn, text, std::get<std::string>(args[2].data)));
    });
}

} // namespace kjdmud

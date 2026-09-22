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
#include "kjdmud/proto/ZmpHandler.hpp"
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

    // Real name confirmed against current FluffOS src/packages/core/
    // core.spec: "void send_msdp_variable(string, string | float | int
    // OR_BUFFER);" - this driver's own earlier "send_msdp" name (fixed
    // this row, GMCP_ENABLE/MSDP_ENABLE's own verification pass) was
    // wrong, unlike has_gmcp/send_gmcp/has_msdp, all three of which
    // already matched the real name. Real signature accepts string,
    // float, int, or buffer for the value; this driver's own MSDP v1
    // scope stays string-only (see Connection.hpp's own sendMsdp()
    // comment on that scope decision), so the string-only union member
    // is what is implemented, not the full real union type.
    t.registerEfun("send_msdp_variable", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::string>(args[1].data)) {
            throw LpcRuntimeError("send_msdp_variable: expected two string arguments (var, value)");
        }
        auto ob = vm.commandGiver();
        if (!ob) ob = vm.currentObject();
        Connection* conn = ob ? InteractiveRegistry::find(ob) : nullptr;
        if (!conn) return Value{};
        MsdpHandler::send(*conn, std::get<std::string>(args[0].data),
                           std::get<std::string>(args[1].data));
        return Value{};
    });

    // Real efun names/signatures, confirmed against current FluffOS
    // src/packages/core/core.spec: "int has_msp(object default:
    // F__THIS_OBJECT);" and "void telnet_msp_oob(string);" - unlike
    // has_gmcp/send_gmcp/has_msdp/send_msdp_variable/has_mxp/mxp_*, no
    // naming deviation here: this row's own verification pass found the
    // real names, so they are used verbatim rather than the has_X/send_X
    // convention this driver invented for the earlier rows when no
    // verified real name was available.
    t.registerEfun("has_msp", [](VM& vm, std::vector<Value>& args) -> Value {
        Connection* conn = connectionFor(vm, args);
        return Value(static_cast<int64_t>(conn && conn->mspEnabled() ? 1 : 0));
    });

    t.registerEfun("telnet_msp_oob", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.empty() || !std::holds_alternative<std::string>(args[0].data)) {
            throw LpcRuntimeError("telnet_msp_oob: expected a string argument");
        }
        // Real f_telnet_msp_oob() reads current_object->interactive, not
        // command_giver: this efun is meant to be called by a user
        // object about itself, unlike send_gmcp/send_msdp above which
        // read command_giver first. Matched here via currentObject()
        // ahead of the command_giver fallback the others use.
        auto ob = vm.currentObject();
        if (!ob) ob = vm.commandGiver();
        Connection* conn = ob ? InteractiveRegistry::find(ob) : nullptr;
        if (!conn) return Value{};
        conn->sendMspOob(std::get<std::string>(args[0].data));
        return Value{};
    });

    // Real efun names/signatures, confirmed against current FluffOS
    // src/packages/core/core.spec: "int has_zmp(object default:
    // F__THIS_OBJECT);" and "void send_zmp(string, string *);" - same
    // "use the verified real name verbatim" precedent MSP's own row set
    // (see has_msp/telnet_msp_oob's own comment above).
    t.registerEfun("has_zmp", [](VM& vm, std::vector<Value>& args) -> Value {
        Connection* conn = connectionFor(vm, args);
        return Value(static_cast<int64_t>(conn && conn->zmpEnabled() ? 1 : 0));
    });

    t.registerEfun("send_zmp", [](VM& vm, std::vector<Value>& args) -> Value {
        if (args.size() < 2 || !std::holds_alternative<std::string>(args[0].data) ||
            !std::holds_alternative<std::shared_ptr<Array>>(args[1].data)) {
            throw LpcRuntimeError("send_zmp: expected (string, string *)");
        }
        // Real f_send_zmp() reads command_giver->interactive, not
        // current_object (the opposite of telnet_msp_oob() just above -
        // confirmed directly, not assumed from that precedent).
        auto ob = vm.commandGiver();
        if (!ob) ob = vm.currentObject();
        Connection* conn = ob ? InteractiveRegistry::find(ob) : nullptr;
        if (!conn) return Value{};
        // Real f_send_zmp() silently skips any non-string array element
        // rather than erroring ("if (sp->u.arr->item[i].type ==
        // T_STRING)"), confirmed directly.
        std::vector<std::string> zmpArgs;
        for (const auto& item : std::get<std::shared_ptr<Array>>(args[1].data)->items) {
            if (std::holds_alternative<std::string>(item.data)) {
                zmpArgs.push_back(std::get<std::string>(item.data));
            }
        }
        ZmpHandler::send(*conn, std::get<std::string>(args[0].data), zmpArgs);
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

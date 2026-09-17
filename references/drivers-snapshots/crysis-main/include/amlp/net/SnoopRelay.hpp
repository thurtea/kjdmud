#pragma once
#include <string>

namespace amlp {

class VM;
class Connection;

// Real comm.c's own "handle_snoop(str, len, who)" macro (add_message() and
// add_vmessage(), both call it unconditionally right after writing to
// ip->message_buf): every text-outputting efun in real FluffOS ultimately
// funnels through one of those two C functions, so a single hook there
// catches all of them -- write(), receive(), printf(), tell_object()'s
// underlying message() path, say(), and (via Server::dispatchLine()) the
// driver's own notify_fail()/notify_no_command() dispatch. This driver has
// no single equivalent chokepoint (EfunTable lambdas only get a VM&, and
// Connection deliberately has no VM access of its own -- see
// InteractiveRegistry.hpp's own comment on why), so every one of those
// call sites routes its outgoing text through this free function instead
// of calling Connection::send() directly.
//
// Confirmed against comm.c's own receive_snoop() (this exact vendored
// build has RECEIVE_SNOOP defined in options.h): duplicated text reaches
// the snooper via an ordinary LPC apply, "receive_snoop(string)", not a
// raw socket write with a "%" prefix (that is the *other*, not-compiled-in
// branch of the same #ifdef). A snooper object that does not define
// receive_snoop() sees nothing at all, matching real apply()-to-an-
// undefined-function silence exactly -- this is not a gap to fix.
//
// Deliberately does not catch an exception thrown by the snooper's own
// receive_snoop() body: real add_message()'s apply() call is not
// insulated from the interpreter's normal error unwinding either (a
// longjmp out of a broken receive_snoop() aborts the *whole* current
// top-level command, including whatever write()/printf()/etc triggered
// it), so this driver lets the same exception propagate back through
// whichever efun called deliverToConnection() rather than swallowing it.
void deliverToConnection(VM& vm, Connection* conn, const std::string& text);

} // namespace amlp

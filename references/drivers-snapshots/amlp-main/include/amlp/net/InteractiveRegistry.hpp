#pragma once
#include <memory>
#include <vector>

namespace amlp {

class LpcObject;
class Connection;

// Every object currently bound to a live connection -- real FluffOS's
// all_users[] array (comm.h/comm.c), which users() (array.c's
// f_users()/livings) and find_player() (otable.c) both search. This
// driver has no single place that already tracks "every connection"
// reachable from an efun (EfunTable lambdas only get a VM&, and VM has
// no Server reference -- constructing one would be circular, Server
// already depends on VM), so this is a small dedicated global registry,
// the same pattern OutputContext already uses for "the connection
// driving the currently executing call". Connection::attach()/close()
// keep it in sync; nothing else needs to.
//
// Also maps each registered object back to its own Connection* (real
// object_t::interactive), not just tracking membership -- needed by
// message()/tell_object() to route output to an arbitrary target object
// rather than only "whichever connection is currently active"
// (OutputContext::current(), which is null for any call not directly
// driven by a live per-connection dispatch -- e.g. every call_out() or
// heart_beat() firing, see message()'s own EfunTable.cpp comment for the
// live bug this closed). The Connection* is a raw, non-owning pointer
// (Server itself owns every Connection's real lifetime); find() returning
// one is only ever used to call send() on it within the same call this
// registry was consulted, never stored past that.
//
// weak_ptr entries: a destructed/closed connection's object must not be
// kept alive by this registry alone, and a stale weak_ptr found during
// all()/find() is simply skipped rather than surfaced as a live user.
class InteractiveRegistry {
public:
    static void add(const std::shared_ptr<LpcObject>& obj, Connection* conn);
    static void remove(const std::shared_ptr<LpcObject>& obj);
    static std::vector<std::shared_ptr<LpcObject>> all();
    static Connection* find(const std::shared_ptr<LpcObject>& obj);
};

} // namespace amlp

#include "aemlpc/net/OutputContext.hpp"

namespace aemlpc {

namespace {
thread_local Connection* g_current = nullptr;
}

void OutputContext::set(Connection* conn) { g_current = conn; }
Connection* OutputContext::current() { return g_current; }

} // namespace aemlpc

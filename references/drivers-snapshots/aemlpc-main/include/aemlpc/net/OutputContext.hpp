#pragma once

namespace aemlpc {

class Connection;

class OutputContext {
public:
    static void set(Connection* conn);
    static Connection* current();
};

} // namespace aemlpc

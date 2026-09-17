#pragma once

namespace kjdmud {

class Connection;

class OutputContext {
public:
    static void set(Connection* conn);
    static Connection* current();
};

} // namespace kjdmud

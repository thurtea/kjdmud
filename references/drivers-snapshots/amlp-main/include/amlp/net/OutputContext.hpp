#pragma once

namespace amlp {

class Connection;

class OutputContext {
public:
    static void set(Connection* conn);
    static Connection* current();
};

} // namespace amlp

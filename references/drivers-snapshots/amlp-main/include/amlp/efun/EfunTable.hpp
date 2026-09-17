#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include "amlp/vm/Value.hpp"

namespace amlp {

class VM;

using EfunFn = std::function<Value(VM&, std::vector<Value>&)>;

class EfunTable {
public:
    static EfunTable& instance();

    void registerEfun(const std::string& name, EfunFn fn);
    Value call(const std::string& name, VM& vm, std::vector<Value>& args) const;
    bool exists(const std::string& name) const;

private:
    std::unordered_map<std::string, EfunFn> table_;
};

void registerCoreEfuns();

} // namespace amlp

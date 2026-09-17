#pragma once
#include <string>
#include <unordered_set>

namespace aemlpc {

class ApplyTable {
public:
    static bool isKnownApply(const std::string& name);

private:
    static const std::unordered_set<std::string>& known();
};

} // namespace aemlpc

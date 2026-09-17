#pragma once
#include <string>
#include <unordered_set>

namespace amlp {

class ApplyTable {
public:
    static bool isKnownApply(const std::string& name);

private:
    static const std::unordered_set<std::string>& known();
};

} // namespace amlp

#include "kjdmud/efun/TestResultsRegistry.hpp"
#include <cstdio>
#include <fstream>

namespace kjdmud {
namespace {

std::vector<TestResultEntry>& buffer() {
    static std::vector<TestResultEntry> entries;
    return entries;
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned char>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    return out;
}

} // namespace

void TestResultsRegistry::clear() { buffer().clear(); }

void TestResultsRegistry::record(const std::string& name, bool passed, const std::string& message) {
    buffer().push_back(TestResultEntry{name, passed, message});
}

const std::vector<TestResultEntry>& TestResultsRegistry::entries() { return buffer(); }

bool TestResultsRegistry::writeJsonFile(const std::string& path) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;
    out << "[";
    bool first = true;
    for (const auto& e : buffer()) {
        if (!first) out << ",";
        first = false;
        out << "{\"name\":\"" << jsonEscape(e.name) << "\","
            << "\"passed\":" << (e.passed ? "true" : "false") << ","
            << "\"message\":\"" << jsonEscape(e.message) << "\"}";
    }
    out << "]";
    return out.good();
}

} // namespace kjdmud

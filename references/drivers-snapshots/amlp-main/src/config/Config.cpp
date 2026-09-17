#include "amlp/config/Config.hpp"
#include <fstream>

namespace amlp {

namespace {
void trim(std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    size_t b = s.find_last_not_of(" \t\r\n");
    if (a == std::string::npos) { s.clear(); return; }
    s = s.substr(a, b - a + 1);
}
} // namespace

bool Config::loadFromFile(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find(':');
        if (pos == std::string::npos) continue;

        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        trim(key);
        trim(val);
        if (key.empty()) continue;

        raw_[key] = val;

        if (key == "mudlib_root") mudlibRoot_ = val;
        else if (key == "master_file") masterFile_ = val;
        else if (key == "port") port_ = std::stoi(val);
        else if (key == "heartbeat_interval_ms") heartbeatIntervalMs_ = std::stoi(val);
        else if (key == "max_eval_cost") maxEvalCost_ = std::stoi(val);
        else if (key == "max_string_length") maxStringLength_ = std::stoi(val);
        else if (key == "include_dir") includeDir_ = val;
        else if (key == "simul_efun_file") simulEfunFile_ = val;
        else if (key == "mud_name") mudName_ = val;
        else if (key == "global_include_file") globalIncludeFile_ = val;
        else if (key == "dialect") dialect_ = val;
        else if (key == "auto_trust_backbone")
            autoTrustBackbone_ = (val == "1" || val == "true" ||
                                  val == "yes" || val == "on");
    }
    return true;
}

} // namespace amlp

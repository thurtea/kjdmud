#pragma once
#include "amlp/dialect/BootApi.hpp"

namespace amlp {

class Config;

// FluffOS/MudOS apply names. This is this driver's current (Phase 0)
// default and only dialect; see src/dialect/instruct.md.
class FluffOsBootApi : public BootApi {
public:
    explicit FluffOsBootApi(const Config& config) : config_(config) {}

    std::string masterFile() const override;
    std::optional<std::string> simulEfunFile() const override;
    std::string logonApply() const override { return "logon"; }
    std::string compileObjectApply() const override { return "compile_object"; }
    std::string privsFileApply() const override { return "privs_file"; }
    std::string heartBeatErrorApply() const override { return "heart_beat_error"; }
    bool hasAutoObject() const override { return false; }
    std::optional<std::string> autoObjectFile() const override { return std::nullopt; }
    std::string masterUidApply() const override { return "get_root_uid"; }

    // No equivalent in real FluffOS -- see BootApi::inaugurateMasterApply()'s
    // own comment for the real source confirming this.
    std::optional<std::string> inaugurateMasterApply() const override { return std::nullopt; }

private:
    const Config& config_;
};

} // namespace amlp

#pragma once
#include "amlp/dialect/BootApi.hpp"

namespace amlp {

class Config;

// LDMud apply names. Corrected 2026-08-22 against a real LDMud 3.6.8
// clone (temp/ldmud, report-only comparison session, no code changes at
// the time) -- see src/dialect/instruct.md and src/apply/instruct.md
// Phase 1.16 for the full citations this class's method bodies are
// drawn from.
class LdmudBootApi : public BootApi {
public:
    explicit LdmudBootApi(const Config& config) : config_(config) {}

    std::string masterFile() const override;
    std::optional<std::string> simulEfunFile() const override;
    std::string logonApply() const override { return "logon"; }
    std::string compileObjectApply() const override { return "compile_object"; }
    std::string privsFileApply() const override { return "privs_file"; }
    std::string heartBeatErrorApply() const override { return "heart_beat_error"; }
    bool hasAutoObject() const override { return false; }
    std::optional<std::string> autoObjectFile() const override { return std::nullopt; }

    // Real LDMud name is "get_master_uid", not FluffOS's "get_root_uid"
    // -- superseded in LDMud 3.2.1@40 (doc/master/get_master_uid's own
    // HISTORY line). An earlier draft of this project's own docs
    // attributed the FluffOS name to LDMud; corrected here.
    std::string masterUidApply() const override { return "get_master_uid"; }

    // Real name, "inaugurate_master" -- see BootApi::inaugurateMasterApply()'s
    // own comment for the full real-source citation.
    std::optional<std::string> inaugurateMasterApply() const override {
        return "inaugurate_master";
    }

private:
    const Config& config_;
};

} // namespace amlp

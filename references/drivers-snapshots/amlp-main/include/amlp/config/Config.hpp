#pragma once
#include <string>
#include <unordered_map>

namespace amlp {

class Config {
public:
    bool loadFromFile(const std::string& path);

    const std::string& mudlibRoot() const { return mudlibRoot_; }
    const std::string& masterFile() const { return masterFile_; }
    int port() const { return port_; }
    int heartbeatIntervalMs() const { return heartbeatIntervalMs_; }
    int maxEvalCost() const { return maxEvalCost_; }
    // Real rc.c's own __MAX_STRING_LENGTH__ config entry (CFG_INT(14),
    // BASE_CONFIG_INT 15, so 14+15=29 -- see EfunTable.cpp's own
    // get_config() comment for the derivation), loaded from the real
    // config file's own "maximum string length" line the same way
    // maxEvalCost_ tracks "max_eval_cost" above. Default matches Dead
    // Souls 3.8.2's own real bundled bin/mudos.cfg ("maximum string
    // length : 200000"), the only real value this driver has a citation
    // for; etc/driver_ds3.cfg also sets it explicitly.
    int maxStringLength() const { return maxStringLength_; }
    const std::string& includeDir() const { return includeDir_; }
    // Empty means "not configured" -- no simul_efun tier at all, matching
    // this being genuinely optional infrastructure (real FluffOS treats
    // it as mandatory at boot, but that's not a useful default for a
    // driver still under incremental construction).
    const std::string& simulEfunFile() const { return simulEfunFile_; }
    // Fed to cpp as the quoted MUD_NAME predefine (see ObjectManager.cpp's
    // buildPredefinedMacroFlags()), matching real FluffOS's lex.c own
    // "add_quoted_predefine(\"MUD_NAME\", MUD_NAME)" sourced from
    // mudos.cfg's "name" setting -- confirmed live:
    // secure/SimulEfun/mud_info.c's own "string mud_name() { return
    // MUD_NAME; }".
    const std::string& mudName() const { return mudName_; }

    // Real FluffOS/MudOS's own "global include file" runtime config
    // option (rc.c's own CONFIG_STR(__GLOBAL_INCLUDE_FILE__), exposed as
    // the GLOBAL_INCLUDE_FILE macro in config.h): when set, the named
    // header is auto-#include'd into every compiled object, before that
    // object's own source, matching real lex.c's own start_new_file()
    // ("if (*GLOBAL_INCLUDE_FILE) { ...; handle_include(gifile, 1); }
    // else refill_buffer();") -- confirmed directly, not guessed. Empty
    // means "not configured" (real semantics too: the check is on the
    // string's own first byte being non-null), a true no-op for any
    // mudlib that never sets it, this repo's own bundled mudlib_stub
    // included.
    const std::string& globalIncludeFile() const { return globalIncludeFile_; }

    // Real FluffOS's own AUTO_TRUST_BACKBONE compile-time option
    // (options.h / local_options: "define this if you want objects with
    // the backbone uid to automatically be trusted and to have their
    // euid set to the uid of the object that forced the object's
    // creation"). It is #undef in the vendored reference build
    // (temp/reference/fluffos-2.9-ds2.08/local_options), so this
    // defaults to false and every existing config file that never sets
    // the key behaves exactly as before. It IS #define'd in three of the
    // vendored mudlib option variants (local_options.lima,
    // local_options.tmi2, local_options.merentha), which is the real
    // corpus evidence for exposing it as a runtime key here. When true,
    // give_uid_to_object()'s middle branch fires: an object whose
    // creator_file() is the backbone uid inherits the loader's euid as
    // both its uid and euid (see resolveObjectUids() in
    // src/security/UidModel.hpp). Only consulted while the uid model is
    // active (a master that defines get_root_uid()).
    bool autoTrustBackbone() const { return autoTrustBackbone_; }

    // Which LPC dialect this driver runs as -- "fluffos" (default),
    // "ldmud", or "dgd" (see src/dialect/LpcDialect.hpp's own
    // dialectFromString() for the string<->enum mapping actually used).
    // Kept as a plain string here, not the LpcDialect enum itself: src/
    // dialect already depends on src/config for Config, so Config
    // depending back on src/dialect would be a library cycle. Parsing
    // happens at the one real call site that needs the enum
    // (src/dialect/DialectSelect.cpp's makeBootApiForConfig()). Defaults
    // to "fluffos" so every existing config file that never sets this
    // key keeps behaving exactly as it did before this key existed.
    const std::string& dialect() const { return dialect_; }

private:
    std::string mudlibRoot_ = "./test/mudlib_stub";
    std::string masterFile_ = "/master";
    int port_ = 1129;
    int heartbeatIntervalMs_ = 2000;
    int maxEvalCost_ = 10000000;
    int maxStringLength_ = 200000;
    std::string includeDir_ = "secure/include";
    std::string simulEfunFile_ = "";
    std::string mudName_ = "AMLP";
    std::string globalIncludeFile_ = "";
    std::string dialect_ = "fluffos";
    bool autoTrustBackbone_ = false;

    std::unordered_map<std::string, std::string> raw_;
};

} // namespace amlp

#include "amlp/object/ObjectManager.hpp"
#include "amlp/object/LivingNameRegistry.hpp"
#include "amlp/object/LiveObjectRegistry.hpp"
#include "amlp/config/Config.hpp"
#include "amlp/core/Errors.hpp"
#include "amlp/compiler/Lexer.hpp"
#include "amlp/compiler/Parser.hpp"
#include "amlp/compiler/CodeGen.hpp"
#include "amlp/dialect/LpcDialect.hpp"
#include "amlp/vm/VM.hpp"
#include <cctype>
#include <fstream>
#include <sstream>
#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#include <algorithm>
#include <chrono>
#include <iterator>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace amlp {

namespace {

// Number of ObjectManager instances currently alive. LiveObjectRegistry
// is process-wide and shared by every manager, so ~ObjectManager only
// runs the registry-wide cycle-break sweep once the last manager goes.
int g_liveManagerCount = 0;

struct PreprocessResult {
    bool ok = false;
    std::string output;
    std::string errorOutput;
};

// option_defs.c predefined-macro table, passed as -D flags because this
// driver shells out to real cpp. Empty value = #ifdef feature flag.
struct PredefinedMacro { const char* name; const char* value; };
constexpr PredefinedMacro kFluffosPredefinedMacros[] = {
    {"__STRIP_BEFORE_PROCESS_INPUT__", ""},
    {"__NO_LIGHT__", ""},
    {"__CUSTOM_CRYPT__", ""},
    {"__RECEIVE_SNOOP__", ""},
    {"__ARGUMENTS_IN_TRACEBACK__", ""},
    {"__NEXT_MALLOC_DEBUG__", ""},
    {"__ARRAY_STATS__", ""},
    {"__LOCALS_IN_TRACEBACK__", ""},
    {"__SYSMALLOC__", ""},
    {"__CFG_MAX_CALL_DEPTH__", "150"},
    {"__SAVE_EXTENSION__", "\\\".o\\\""},
    {"__LOG_CATCHES__", ""},
    {"__NONINTERACTIVE_STDERR_WRITE__", ""},
    {"__SMALL_STRING_SIZE__", "100"},
    {"__CALLOUT_CYCLE_SIZE__", "32"},
    {"__PACKAGE_MATH__", ""},
    {"__PACKAGE_DEVELOP__", ""},
    {"__SUPPRESS_ARGUMENT_WARNINGS__", ""},
    {"__LARGEST_PRINTABLE_STRING__", "8192"},
    {"__CFG_LIVING_HASH_SIZE__", "256"},
    {"__CACHE_STATS__", ""},
    {"__CONFIG_FILE_DIR__", "\\\"./\\\""},
    {"__PARSE_DEBUG__", ""},
    {"__TRAP_CRASHES__", ""},
    {"__RESTRICTED_ED__", ""},
    {"__STRING_STATS__", ""},
    {"__CALLOUT_HANDLES__", ""},
    {"__FLUFFOS__", ""},
    {"__CFG_COMPILER_STACK_SIZE__", "600000"},
    {"__LARGE_STRING_SIZE__", "1000"},
    {"__HEARTBEAT_INTERVAL__", "1"},
    {"__PACKAGE_MATRIX__", ""},
    {"__PRIVS__", ""},
    {"__COMMAND_BUF_SIZE__", "2000"},
    {"__OLD_ED__", ""},
    {"__PACKAGE_PARSER__", ""},
    {"__THIS_PLAYER_IN_CALL_OUT__", ""},
    {"__PACKAGE_CONTRIB__", ""},
    {"__ALLOW_INHERIT_AFTER_GLOBAL_VARIABLES__", ""},
    {"__CFG_MAX_LOCAL_VARIABLES__", "50"},
    {"__MESSAGE_BUFFER_SIZE__", "4096"},
    {"__NO_WIZARDS__", ""},
    {"__SAVE_GZ_EXTENSION__", "\\\".o.gz\\\""},
    {"__PACKAGE_SOCKETS__", ""},
    {"__NUM_EXTERNAL_CMDS__", "100"},
    {"__HEART_BEAT_CHUNK__", "32"},
    {"__INTERACTIVE_CATCH_TELL__", ""},
    {"__MAX_SAVE_SVALUE_DEPTH__", "100"},
    {"__DEFAULT_PRAGMAS__", "0"},
    {"__NO_ANSI__", ""},
    {"__HAS_STATUS_TYPE__", ""},
    {"__CFG_EVALUATOR_STACK_SIZE__", "3000"},
    {"__PACKAGE_MUDLIB_STATS__", ""},
    {"__WARN_TAB__", ""},
    {"__ALLOW_INHERIT_AFTER_FUNCTION__", ""},
    {"__APPLY_CACHE_BITS__", "11"},
    {"__MUDLIB_ERROR_HANDLER__", ""},
};

// lex.c add_predefines(): boot-computed values (version, arch, ...).
// MUD_NAME/__PORT__ come from Config, not this table.
constexpr PredefinedMacro kFluffosRuntimePredefinedMacros[] = {
    {"MUDOS", ""},
    // No spaces: popen() below is one unquoted shell command, so a
    // space would word-split into extra nonexistent input filenames.
    {"__VERSION__", "\\\"2.9-ds2.08\\\""},
    {"__ARCH__", "\\\"Linux\\\""},
    {"__COMPILER__", "\\\"g++\\\""},
    {"__OPTIMIZATION__", "\\\"-O2\\\""},
    // lex.c "sizeof(long)" / "(long)1<<63 - 1" on 64-bit Linux.
    {"SIZEOFINT", "8"},
    {"MAX_INT", "9223372036854775807"},
    // HAS_ED/HAS_PRINTF/HAS_RUSAGE/HAS_DEBUG_LEVEL omitted: lex.c only
    // defines each when that feature is compiled in, and none are here.
};

// PCRE_* from temp/fluffos src/include/pcre_flags.h (not in 2.9-ds2.08).
// Decimal literals, not "(1 << 16)": unescaped parens would open a
// subshell in the unquoted popen() -D flags below.
constexpr PredefinedMacro kPcrePackagePredefinedMacros[] = {
    {"__PACKAGE_PCRE__", ""},
    {"PCRE_DEFAULT", "0"},
    {"PCRE_I", "65536"},
    {"PCRE_M", "131072"},
    {"PCRE_S", "262144"},
    {"PCRE_U", "524288"},
    {"PCRE_X", "1048576"},
    {"PCRE_A", "2097152"},
};

// compiledFilename is the normalized LPC path (leading '/', no ".c").
std::string buildPredefinedMacroFlags(const Config& config, const std::string& compiledFilename) {
    std::ostringstream flags;
    for (const auto& macro : kFluffosPredefinedMacros) {
        flags << " -D" << macro.name << "=" << macro.value;
    }
    for (const auto& macro : kFluffosRuntimePredefinedMacros) {
        flags << " -D" << macro.name << "=" << macro.value;
    }
    for (const auto& macro : kPcrePackagePredefinedMacros) {
        flags << " -D" << macro.name << "=" << macro.value;
    }
    flags << " -D__PORT__=" << config.port();
    flags << " -DMUD_NAME=\\\"" << config.mudName() << "\\\"";

    // lex.c start_new_file(): __FILE__ is the LPC path with ".c",
    // __DIR__ is that truncated after the last '/'. Forced -D so gcc's
    // built-in __FILE__ (a host filesystem path) does not win.
    std::string lpcFile = compiledFilename + ".c";
    std::string lpcDir = lpcFile.substr(0, lpcFile.find_last_of('/') + 1);
    flags << " -D__FILE__=\\\"" << lpcFile << "\\\"";
    flags << " -D__DIR__=\\\"" << lpcDir << "\\\"";
    return flags.str();
}

// Forward declaration: defined further down, needed by
// rewriteAbsoluteIncludesRecursive() below for every spliced-in file.
std::string maskHashQuote(const std::string& source);

// Splice every #include (quoted, angle-bracket, or a bare macro name)
// into the source before cpp runs. Real cpp has no mudlib root, so a
// leading '/' would hit the filesystem root. Recurses so nested and
// computed includes (`#include CONFIG_H`, C99 6.10.2p4) resolve;
// missing/cyclic targets are left for cpp's own diagnostic. currentDir
// is the quote-form search directory of the file being spliced.
std::string rewriteAbsoluteIncludesRecursive(const std::string& source, const std::string& mudlibRoot,
                                              const std::vector<std::string>& includeDirs,
                                              const std::string& currentDir,
                                              std::unordered_set<std::string>& activeIncludes,
                                              std::unordered_map<std::string, std::string>& macroDefs,
                                              int depth) {
    // Safety net against pathological cycles (already guarded by
    // activeIncludes). Deepest real corpus case seen is depth 2 or 3.
    if (depth > 64) return source;

    std::istringstream in(source);
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line)) {
        bool spliced = false;
        size_t hashPos = line.find_first_not_of(" \t");
        if (hashPos != std::string::npos && line[hashPos] == '#') {
            // Match the directive keyword, not the word "include"/"define"
            // anywhere on the line (a #define value can contain those).
            size_t kwStart = line.find_first_not_of(" \t", hashPos + 1);
            size_t kwEnd = kwStart;
            while (kwEnd != std::string::npos && kwEnd < line.size() &&
                   std::isalpha(static_cast<unsigned char>(line[kwEnd]))) {
                ++kwEnd;
            }
            bool isDefine = kwStart != std::string::npos &&
                             line.compare(kwStart, kwEnd - kwStart, "define") == 0;
            bool isInclude = kwStart != std::string::npos &&
                              line.compare(kwStart, kwEnd - kwStart, "include") == 0;

            if (isDefine) {
                // Record "#define NAME "value"" only; anything else is
                // left for cpp.
                size_t nameStart = line.find_first_not_of(" \t", kwEnd);
                if (nameStart != std::string::npos) {
                    size_t nameEnd = nameStart;
                    while (nameEnd < line.size() &&
                           (std::isalnum(static_cast<unsigned char>(line[nameEnd])) || line[nameEnd] == '_')) {
                        ++nameEnd;
                    }
                    if (nameEnd > nameStart && (nameEnd >= line.size() || line[nameEnd] != '(')) {
                        std::string macroName = line.substr(nameStart, nameEnd - nameStart);
                        size_t q1 = line.find('"', nameEnd);
                        if (q1 != std::string::npos) {
                            size_t q2 = line.find('"', q1 + 1);
                            if (q2 != std::string::npos) {
                                macroDefs[macroName] = line.substr(q1 + 1, q2 - q1 - 1);
                            }
                        }
                    }
                }
            } else if (isInclude) {
                // Target text and delimiter, or a bare macro name already
                // recorded by the #define branch (computed include).
                size_t quotePos = line.find('"', kwEnd);
                size_t anglePos = line.find('<', kwEnd);
                std::string targetPath;
                char delim = 0;
                bool viaMacro = false;
                std::string macroExpandedLine;

                if (quotePos != std::string::npos) {
                    size_t endQuote = line.find('"', quotePos + 1);
                    if (endQuote != std::string::npos) {
                        targetPath = line.substr(quotePos + 1, endQuote - quotePos - 1);
                        delim = '"';
                    }
                } else if (anglePos != std::string::npos) {
                    size_t endAngle = line.find('>', anglePos + 1);
                    if (endAngle != std::string::npos) {
                        targetPath = line.substr(anglePos + 1, endAngle - anglePos - 1);
                        delim = '<';
                    }
                } else {
                    size_t nameStart = line.find_first_not_of(" \t", kwEnd);
                    if (nameStart != std::string::npos) {
                        size_t nameEnd = line.find_last_not_of(" \t\r") + 1;
                        if (nameEnd > nameStart) {
                            std::string macroName = line.substr(nameStart, nameEnd - nameStart);
                            auto it = macroDefs.find(macroName);
                            if (it != macroDefs.end()) {
                                targetPath = it->second;
                                delim = '"'; // macroDefs only ever records a quoted value
                                viaMacro = true;
                                macroExpandedLine = line.substr(0, kwStart) + "include \"" + targetPath + "\"";
                            }
                        }
                    }
                }

                if (delim != 0) {
                    bool isAbsolute = !targetPath.empty() && targetPath[0] == '/';
                    std::string realPath;
                    bool found = false;
                    if (isAbsolute) {
                        realPath = mudlibRoot + targetPath;
                        std::ifstream probe(realPath);
                        found = static_cast<bool>(probe);
                    } else {
                        // Quote-search: including file's directory first,
                        // '"' form only; then configured -I dirs.
                        std::vector<std::string> searchDirs;
                        if (delim == '"' && !currentDir.empty()) searchDirs.push_back(currentDir);
                        for (const auto& d : includeDirs) searchDirs.push_back(d);
                        for (const auto& d : searchDirs) {
                            std::string candidate = d + "/" + targetPath;
                            std::ifstream probe(candidate);
                            if (probe) {
                                realPath = candidate;
                                found = true;
                                break;
                            }
                        }
                    }

                    if (found && !activeIncludes.count(realPath)) {
                        std::ifstream target(realPath);
                        std::ostringstream targetBuf;
                        targetBuf << target.rdbuf();
                        activeIncludes.insert(realPath);
                        std::string targetDir = realPath.substr(0, realPath.find_last_of('/'));
                        out << rewriteAbsoluteIncludesRecursive(
                                   maskHashQuote(targetBuf.str()), mudlibRoot, includeDirs, targetDir,
                                   activeIncludes, macroDefs, depth + 1)
                            << "\n";
                        activeIncludes.erase(realPath);
                        spliced = true;
                    }

                    if (!spliced) {
                        // Missing or cyclic: leave for cpp. Absolute
                        // targets are rewritten mudlib-root-relative;
                        // macro-computed targets are expanded in place.
                        if (isAbsolute) {
                            line = viaMacro ? macroExpandedLine : line;
                            size_t p = line.find('"', kwEnd);
                            if (p != std::string::npos && p + 1 < line.size() && line[p + 1] == '/') {
                                line.insert(p + 1, mudlibRoot);
                            }
                        } else if (viaMacro) {
                            line = macroExpandedLine;
                        }
                    }
                }
            }
        }
        if (!spliced) out << line << "\n";
    }
    return out.str();
}

// Rewrite driver-internal efun_defined(name) (lex.c:3040) to 1/0 on
// #if/#elif lines before cpp sees it. Real cpp would expand the bare
// identifier to 0 and then fail with "missing binary operator".
std::string rewriteEfunDefined(const std::string& source,
                                const std::function<bool(const std::string&)>& efunExists) {
    std::istringstream in(source);
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line)) {
        size_t hashPos = line.find_first_not_of(" \t");
        if (hashPos != std::string::npos && line[hashPos] == '#') {
            size_t kwStart = line.find_first_not_of(" \t", hashPos + 1);
            size_t kwEnd = kwStart;
            while (kwEnd != std::string::npos && kwEnd < line.size() &&
                   std::isalpha(static_cast<unsigned char>(line[kwEnd]))) {
                ++kwEnd;
            }
            bool isIfOrElif = kwStart != std::string::npos &&
                (line.compare(kwStart, kwEnd - kwStart, "if") == 0 ||
                 line.compare(kwStart, kwEnd - kwStart, "elif") == 0);
            if (isIfOrElif) {
                size_t searchFrom = kwEnd;
                size_t callPos;
                while ((callPos = line.find("efun_defined", searchFrom)) != std::string::npos) {
                    size_t nameStart = line.find_first_not_of(" \t", callPos + 12);
                    if (nameStart == std::string::npos || line[nameStart] != '(') {
                        searchFrom = callPos + 12;
                        continue;
                    }
                    nameStart = line.find_first_not_of(" \t", nameStart + 1);
                    size_t nameEnd = nameStart;
                    while (nameEnd != std::string::npos && nameEnd < line.size() &&
                           (std::isalnum(static_cast<unsigned char>(line[nameEnd])) || line[nameEnd] == '_')) {
                        ++nameEnd;
                    }
                    size_t closeParen = line.find_first_not_of(" \t", nameEnd);
                    if (nameStart == std::string::npos || nameEnd == nameStart ||
                        closeParen == std::string::npos || line[closeParen] != ')') {
                        searchFrom = callPos + 12;
                        continue;
                    }
                    std::string efunName = line.substr(nameStart, nameEnd - nameStart);
                    std::string replacement = efunExists(efunName) ? "1" : "0";
                    line.replace(callPos, closeParen + 1 - callPos, replacement);
                    searchFrom = callPos + replacement.size();
                }
            }
        }
        out << line << "\n";
    }
    return out.str();
}

// Mask LDMud "#'name" closure literals so system cpp does not treat
// them as unknown directives. Unmasked after cpp returns.
const std::string kHashQuoteMarker = "__AMLP_HASHQUOTE_MARKER__";

std::string maskHashQuote(const std::string& source) {
    std::string result;
    result.reserve(source.size());
    for (size_t i = 0; i < source.size(); ++i) {
        if (source[i] == '#' && i + 1 < source.size() && source[i + 1] == '\'') {
            result += kHashQuoteMarker;
            ++i; // also consume the '\'' -- the marker stands in for both characters
        } else {
            result += source[i];
        }
    }
    return result;
}

std::string unmaskHashQuote(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    size_t pos = 0;
    while (pos < text.size()) {
        if (text.compare(pos, kHashQuoteMarker.size(), kHashQuoteMarker) == 0) {
            result += "#'";
            pos += kHashQuoteMarker.size();
        } else {
            result += text[pos];
            ++pos;
        }
    }
    return result;
}

struct StagedSource {
    bool ok = false;
    std::string tempPath;
    std::string errorMessage;
};

// Write rewritten source to a temp file for cpp. Prepend a global
// include file when configured (lex.c start_new_file()), then a
// `# 1 "originalPath"` marker so errors cite the real file. Angle-
// bracket global includes are spliced here so nested computed includes
// resolve; a missing header falls back to a plain #include.
StagedSource stageSourceForPreprocessing(const std::string& originalPath,
                                          const std::string& mudlibRoot,
                                          const std::string& globalIncludeFile,
                                          const std::vector<std::string>& includeDirs,
                                          const std::function<bool(const std::string&)>& efunExists) {
    StagedSource result;

    std::ifstream f(originalPath);
    if (!f) {
        result.errorMessage = "source file not found: " + originalPath;
        return result;
    }
    std::ostringstream buf;
    buf << f.rdbuf();
    f.close();

    // Shared with the global-include splice: cpp macro scope is
    // whole-compilation, not per-file.
    std::unordered_set<std::string> activeIncludes;
    std::unordered_map<std::string, std::string> macroDefs;

    std::string prefix;
    if (!globalIncludeFile.empty()) {
        bool splicedGlobalHeader = false;
        if (globalIncludeFile.size() >= 2 && globalIncludeFile.front() == '<' &&
            globalIncludeFile.back() == '>') {
            std::string headerName = globalIncludeFile.substr(1, globalIncludeFile.size() - 2);
            for (const auto& dir : includeDirs) {
                std::ifstream header(dir + "/" + headerName);
                if (header) {
                    std::ostringstream headerBuf;
                    headerBuf << header.rdbuf();
                    prefix = rewriteAbsoluteIncludesRecursive(
                        maskHashQuote(headerBuf.str()), mudlibRoot, includeDirs, dir, activeIncludes, macroDefs, 0);
                    splicedGlobalHeader = true;
                    break;
                }
            }
        }
        if (!splicedGlobalHeader) {
            // No header found to splice; mudlibRoot stands in as
            // currentDir because the signature requires one.
            prefix = rewriteAbsoluteIncludesRecursive("#include " + globalIncludeFile + "\n",
                                                        mudlibRoot, includeDirs, mudlibRoot, activeIncludes, macroDefs, 0);
        }
    }

    std::string originalSourceDir = originalPath.substr(0, originalPath.find_last_of('/'));
    std::string rewritten = prefix + "# 1 \"" + originalPath + "\"\n" +
        rewriteAbsoluteIncludesRecursive(maskHashQuote(buf.str()), mudlibRoot, includeDirs, originalSourceDir,
                                          activeIncludes, macroDefs, 0);
    rewritten = rewriteEfunDefined(rewritten, efunExists);

    char tmpPathTemplate[] = "/tmp/amlp_src_XXXXXX";
    int fd = mkstemp(tmpPathTemplate);
    if (fd == -1) {
        result.errorMessage = "failed to create temp file for staged source";
        return result;
    }
    std::string tmpPath = tmpPathTemplate;

    ssize_t written = write(fd, rewritten.data(), rewritten.size());
    close(fd);
    if (written < 0 || static_cast<size_t>(written) != rewritten.size()) {
        std::remove(tmpPath.c_str());
        result.errorMessage = "failed to write staged source to temp file";
        return result;
    }

    result.ok = true;
    result.tempPath = tmpPath;
    return result;
}

// Drop cpp line markers (`# 1 "file.h"`); they are not LPC.
std::string stripLineMarkers(const std::string& text) {
    std::istringstream in(text);
    std::ostringstream out;
    std::string line;
    while (std::getline(in, line)) {
        size_t i = line.find_first_not_of(" \t");
        if (i != std::string::npos && line[i] == '#') continue;
        out << line << "\n";
    }
    return out.str();
}

// mudos.cfg "include directories" is a colon-separated list
// (rc.c CONFIG_STR(__INCLUDE_DIRS__)/set_inc_list()), not a single path.
std::vector<std::string> splitIncludeDirs(const std::string& raw, const std::string& mudlibRoot) {
    std::vector<std::string> dirs;
    size_t start = 0;
    while (start <= raw.size()) {
        size_t colon = raw.find(':', start);
        std::string entry = raw.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
        if (!entry.empty()) {
            if (entry[0] != '/') entry = mudlibRoot + "/" + entry;
            dirs.push_back(entry);
        }
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return dirs;
}

PreprocessResult runPreprocessor(const std::string& sourcePath, const std::vector<std::string>& includeDirs,
                                  const std::string& originalSourceDir, const Config& config,
                                  const std::string& compiledFilename) {
    PreprocessResult result;

    char errPathTemplate[] = "/tmp/amlp_cpp_stderr_XXXXXX";
    int errFd = mkstemp(errPathTemplate);
    if (errFd == -1) {
        result.errorOutput = "failed to create temp file for cpp stderr output";
        return result;
    }
    close(errFd);
    std::string errPath = errPathTemplate;

    // sourcePath is a /tmp copy, so -I originalSourceDir restores
    // same-directory quoted includes. -I '.' covers absolute includes
    // rewritten to a CWD-relative (mudlibRoot-prepended) path.
    std::string cmd = "cpp -I '.' -I '" + originalSourceDir + "'";
    for (const auto& dir : includeDirs) {
        cmd += " -I '" + dir + "'";
    }
    cmd += buildPredefinedMacroFlags(config, compiledFilename) +
           " -x c '" + sourcePath + "' 2>'" + errPath + "'";

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        result.errorOutput = "failed to launch cpp preprocessor";
        std::remove(errPath.c_str());
        return result;
    }

    std::ostringstream outBuf;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), pipe)) > 0) {
        outBuf.write(buf, static_cast<std::streamsize>(n));
    }
    int status = pclose(pipe);

    std::ifstream errFile(errPath);
    std::ostringstream errBuf;
    errBuf << errFile.rdbuf();
    std::remove(errPath.c_str());
    result.errorOutput = errBuf.str();

    // Success is the exit code, not "stderr is empty": cpp writes
    // real warnings there too (e.g. "#endif LABEL" in debug.h).
    bool exitedOk = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    result.output = unmaskHashQuote(stripLineMarkers(outBuf.str()));
    result.ok = exitedOk;
    return result;
}

} // namespace

ObjectManager::ObjectManager(Config& config) : config_(config) { ++g_liveManagerCount; }

std::string ObjectManager::normalizeFilename(const std::string& filename) {
    std::string result = filename;
    if (result.size() >= 2 && result.compare(result.size() - 2, 2, ".c") == 0) {
        result = result.substr(0, result.size() - 2);
    }
    // LPC pathnames are mudlib-root-relative with or without a leading
    // '/'. Prepend '/' when absent so filesystem joins stay well-formed.
    if (result.empty() || result[0] != '/') {
        result.insert(result.begin(), '/');
    }
    return result;
}

std::shared_ptr<CompiledProgram> ObjectManager::compile(const std::string& rawFilename) {
    std::string filename = normalizeFilename(rawFilename);
    std::string path = config_.mudlibRoot() + filename + ".c";

    // Read current bytes before any cache hit so same-path rewrite is
    // detected (eval.c's rm/destruct/write_file/reload cycle).
    std::ifstream f(path);
    bool sourceExists = static_cast<bool>(f);
    std::string currentSource;
    if (sourceExists) {
        std::ostringstream buf;
        buf << f.rdbuf();
        currentSource = buf.str();
    }
    f.close();

    auto cached = programCache_.find(filename);
    if (cached != programCache_.end()) {
        auto sourceIt = programSource_.find(filename);
        bool sourceUnchanged =
            sourceExists && sourceIt != programSource_.end() && sourceIt->second == currentSource;
        // Missing source: keep serving the last good program.
        if (sourceUnchanged || !sourceExists) return cached->second;
        // Bytes differ: recompile below. Live objects holding the old
        // CompiledProgram via shared_ptr are unaffected.
    }

    if (compiling_.count(filename)) {
        std::cerr << "[object] inherit cycle detected involving " << filename << "\n";
        return nullptr;
    }
    compiling_.insert(filename);
    struct InProgressGuard {
        std::unordered_set<std::string>& set;
        const std::string& filename;
        ~InProgressGuard() { set.erase(filename); }
    } inProgressGuard{compiling_, filename};

    if (!sourceExists) {
        std::cerr << "[object] source file not found: " << path << "\n";
        return nullptr;
    }

    std::vector<std::string> includeDirs = splitIncludeDirs(config_.includeDir(), config_.mudlibRoot());

    StagedSource staged =
        stageSourceForPreprocessing(path, config_.mudlibRoot(), config_.globalIncludeFile(), includeDirs,
                                     efunExistsChecker_);
    if (!staged.ok) {
        std::cerr << "[object] " << staged.errorMessage << "\n";
        return nullptr;
    }
    std::string originalSourceDir = path.substr(0, path.find_last_of('/'));
    PreprocessResult preprocessed =
        runPreprocessor(staged.tempPath, includeDirs, originalSourceDir, config_, filename);
    std::remove(staged.tempPath.c_str());
    if (!preprocessed.ok) {
        std::cerr << "[object] preprocessing failed for " << path << ":\n"
                   << preprocessed.errorOutput << "\n";
        return nullptr;
    }

    try {
        // The one call site that feeds Config::dialect() into Lexer/Parser.
        LpcDialect dialect = dialectFromString(config_.dialect());
        Lexer lexer(preprocessed.output, dialect);
        auto tokens = lexer.tokenize();
        Parser parser(std::move(tokens), dialect);
        auto ast = parser.parseProgram();

        // Compile inherit targets first so CodeGen can flatten parent
        // object vars (CodeGen::generate inheritedObjectVarNames).
        // ancestorBaseOffsets compose each parent's own offsets by the
        // running base (Bytecode.hpp CompiledProgram::ancestorBaseOffsets).
        std::vector<std::shared_ptr<CompiledProgram>> parents;
        std::vector<std::string> inheritedObjectVarNames;
        std::unordered_map<const CompiledProgram*, int> ancestorBaseOffsets;
        for (const auto& inheritPath : ast->inherits) {
            auto parentProgram = compile(inheritPath);
            if (!parentProgram) {
                std::cerr << "[object] " << path << ": failed to compile inherited file \""
                          << inheritPath << "\"\n";
                return nullptr;
            }
            int base = static_cast<int>(inheritedObjectVarNames.size());
            ancestorBaseOffsets[parentProgram.get()] = base;
            for (const auto& entry : parentProgram->ancestorBaseOffsets) {
                ancestorBaseOffsets[entry.first] = base + entry.second;
            }
            parents.push_back(parentProgram);
            inheritedObjectVarNames.insert(inheritedObjectVarNames.end(),
                                            parentProgram->objectVarNames.begin(),
                                            parentProgram->objectVarNames.end());
        }

        CodeGen codegen;
        auto program = std::make_shared<CompiledProgram>(
            codegen.generate(*ast, inheritedObjectVarNames));
        program->inheritedPrograms = std::move(parents);
        program->ancestorBaseOffsets = std::move(ancestorBaseOffsets);

        programCache_[filename] = program;
        programSource_[filename] = currentSource;
        return program;
    } catch (const LpcRuntimeError& e) {
        std::cerr << "[object] compile error in " << path << ": " << e.what() << "\n";
        return nullptr;
    } catch (const std::exception& e) {
        std::cerr << "[object] unexpected error compiling " << path << ": " << e.what() << "\n";
        return nullptr;
    }
}

bool ObjectManager::loadMasterObject() {
    master_ = loadObject(config_.masterFile());
    if (master_) captureBootUids();
    return master_ != nullptr;
}

void ObjectManager::captureBootUids() {
    if (!vm_ || !master_) return;

    // master.c:108 apply_master_ob(APPLY_GET_ROOT_UID). A missing
    // get_root_uid() leaves the model inactive (real exits(-1)).
    Value rootRet;
    try {
        rootRet = vm_->callFunction(master_, "get_root_uid", {});
        if (std::holds_alternative<std::monostate>(rootRet.data)) {
            // LDMud 3.2.1@40 renamed this apply get_master_uid.
            rootRet = vm_->callFunction(master_, "get_master_uid", {});
        }
    } catch (const std::exception&) {
        return;
    }
    auto* rootStr = std::get_if<std::string>(&rootRet.data);
    if (!rootStr || rootStr->empty()) return;
    uidModel_.rootUid = *rootStr;

    // master.c:126 APPLY_GET_BACKBONE_UID ("get_bb_uid"). Missing is
    // not fatal; only AUTO_TRUST_BACKBONE consumes it.
    try {
        Value bbRet = vm_->callFunction(master_, "get_bb_uid", {});
        if (auto* bb = std::get_if<std::string>(&bbRet.data); bb && !bb->empty()) {
            uidModel_.backboneUid = *bb;
        }
    } catch (const std::exception&) {
        // leave backboneUid unset
    }

    // AUTO_TRUST_BACKBONE compile flag, read from config (default false).
    uidModel_.autoTrustBackbone = config_.autoTrustBackbone();

    // master.c:121-122 master_ob->uid = set_root_uid(...); euid = uid.
    master_->setUid(*rootStr);
    master_->setEuid(*rootStr);
}

void ObjectManager::assignObjectUid(const std::shared_ptr<LpcObject>& obj,
                                     const std::string& filename) {
    if (!obj || !vm_ || !master_ || !uidModel_.active()) return;

    // simulate.c:149-151 apply_master_ob(APPLY_CREATOR_FILE, 1).
    std::string slashPath =
        (filename.empty() || filename[0] == '/') ? filename : ("/" + filename);
    std::string creatorName;
    bool haveCreator = false;
    if (vm_->functionExists(master_, "creator_file")) {
        try {
            Value ret = vm_->callFunction(master_, "creator_file", {Value(slashPath)});
            if (auto* s = std::get_if<std::string>(&ret.data)) {
                creatorName = *s;
                haveCreator = true;
            }
        } catch (const std::exception&) {
            // fall through to the safe default below
        }
    }
    if (!haveCreator) {
        // Divergence from simulate.c:157-160, which destructs and
        // errors. Keep the object root-owned so a mudlib with
        // get_root_uid() but no working creator_file() still boots.
        obj->setUid(*uidModel_.rootUid);
        return;
    }

    auto caller = vm_->currentObject();
    std::optional<std::string> loaderUid = caller ? caller->uid() : uidModel_.rootUid;
    std::optional<std::string> loaderEuid = caller ? caller->euid() : uidModel_.rootUid;

    ResolvedObjectUids r =
        resolveObjectUids(uidModel_, creatorName, loaderUid, loaderEuid);
    obj->setUid(r.uid);
    obj->setEuid(r.euid);
}

bool ObjectManager::loadSimulEfunObject() {
    if (config_.simulEfunFile().empty()) return false;
    simulEfunObject_ = loadObject(config_.simulEfunFile());
    return simulEfunObject_ != nullptr;
}

bool ObjectManager::sourceFileExists(const std::string& rawFilename) const {
    std::string filename = normalizeFilename(rawFilename);
    std::string path = config_.mudlibRoot() + filename + ".c";
    struct stat st;
    // simulate.c int_load_object(): a directory of the same name is
    // not a source file.
    return ::stat(path.c_str(), &st) == 0 && !S_ISDIR(st.st_mode);
}

std::shared_ptr<LpcObject> ObjectManager::loadVirtualObject(const std::string& filename) {
    // simulate.c load_virtual_object(): no master yet, nothing to ask.
    if (!master_ || !vm_) return nullptr;

    if (virtualCompiling_.count(filename)) {
        std::cerr << "[object] compile_object() recursion detected for " << filename << "\n";
        return nullptr;
    }
    virtualCompiling_.insert(filename);
    struct InProgressGuard {
        std::unordered_set<std::string>& set;
        const std::string& filename;
        ~InProgressGuard() { set.erase(filename); }
    } inProgressGuard{virtualCompiling_, filename};

    // simulate.c: APPLY_COMPILE_OBJECT with clone=0 (int_load_object).
    Value result;
    try {
        result = vm_->applyMaster("compile_object", {Value(filename), Value(int64_t{0})});
    } catch (const std::exception& e) {
        std::cerr << "[object] compile_object(" << filename << ") failed: " << e.what() << "\n";
        return nullptr;
    }

    if (!std::holds_alternative<std::shared_ptr<LpcObject>>(result.data)) {
        // Real: non-object return means the path does not exist.
        return nullptr;
    }
    auto ob = std::get<std::shared_ptr<LpcObject>>(result.data);
    if (!ob) return nullptr;

    // load_virtual_object(): rebind to the virtual path (SETOBNAME +
    // enter_object_hash()) and set O_VIRTUAL.
    ob->rebindFilename(filename);
    loaded_[filename] = ob;
    ob->setIsVirtual(true);
    return ob;
}

std::shared_ptr<LpcObject> ObjectManager::loadObject(const std::string& rawFilename) {
    std::string filename = normalizeFilename(rawFilename);
    auto existing = loaded_.find(filename);
    if (existing != loaded_.end()) return existing->second;

    if (!sourceFileExists(filename)) {
        auto virtualObj = loadVirtualObject(filename);
        if (virtualObj) return virtualObj;
        std::cerr << "[object] source file not found: "
                   << config_.mudlibRoot() << filename << ".c"
                   << " (and compile_object() did not provide a virtual object)\n";
        return nullptr;
    }

    auto program = compile(filename);
    if (!program) return nullptr;

    // FluffOS T_UNDEFINED gate (Value.hpp isUndefined). LDMud: plain 0.
    auto obj = std::make_shared<LpcObject>(filename, program, config_.dialect() == "fluffos");
    loaded_[filename] = obj;
    LiveObjectRegistry::add(obj);
    initPrivsForObject(obj, filename);
    // init_object() -> give_uid_to_object() (simulate.c), before create().
    assignObjectUid(obj, filename);

    // create() throwing fails this load, not the whole process.
    if (vm_) {
        try {
            runObjectVarInitializers(obj, *program);
            vm_->callFunction(obj, "create", {});
        } catch (const std::exception& e) {
            std::cerr << "[object] create() failed for " << filename << ": " << e.what() << "\n";
            loaded_.erase(filename);
            return nullptr;
        }
    }
    armResetAndCleanup(obj);

    return obj;
}

void ObjectManager::runObjectVarInitializers(const std::shared_ptr<LpcObject>& obj,
                                              const CompiledProgram& program) {
    if (!vm_) return;
    for (const auto& parent : program.inheritedPrograms) {
        if (parent) runObjectVarInitializers(obj, *parent);
    }
    vm_->callFunctionInProgram(obj, program, "$objvarinit", {});
}

std::shared_ptr<LpcObject> ObjectManager::cloneObject(const std::string& rawFilename) {
    // simulate.c:545-549: refuse clone_object() when the caller's euid
    // is NULL (PACKAGE_UIDS / uidModel_.active()). Message is verbatim.
    if (uidModel_.active() && vm_) {
        auto callerOb = vm_->currentObject();
        if (callerOb && !callerOb->euid().has_value()) {
            throw LpcRuntimeError(
                "Object must call seteuid() prior to calling clone_object().\n");
        }
    }

    std::string filename = normalizeFilename(rawFilename);
    auto program = compile(filename);
    if (!program) return nullptr;

    // Same real FluffOS T_UNDEFINED gate as loadObject() above.
    auto obj = std::make_shared<LpcObject>(filename, program, config_.dialect() == "fluffos");
    LiveObjectRegistry::add(obj);
    initPrivsForObject(obj, filename);
    // clone_object() -> give_uid_to_object(new_ob) (simulate.c:309).
    assignObjectUid(obj, filename);
    // simulate.c:2448: O_CLONE | O_WILL_CLEAN_UP at construction.
    obj->setIsClone(true);

    if (vm_) {
        try {
            runObjectVarInitializers(obj, *program);
            vm_->callFunction(obj, "create", {});
        } catch (const std::exception& e) {
            std::cerr << "[object] create() failed for " << filename << ": " << e.what() << "\n";
            return nullptr;
        }
    }
    armResetAndCleanup(obj);

    return obj;
}

void ObjectManager::initPrivsForObject(const std::shared_ptr<LpcObject>& obj, const std::string& filename) {
    if (!vm_ || !master_ || !obj) return;
    Value result;
    try {
        result = vm_->applyMaster("privs_file", {Value(filename)});
    } catch (const std::exception&) {
        // Non-string (including a thrown apply) leaves privs unset.
        return;
    }
    if (auto* s = std::get_if<std::string>(&result.data)) {
        obj->setPrivs(*s);
    }
}

std::shared_ptr<LpcObject> ObjectManager::lookupLoadedObject(const std::string& rawFilename) const {
    auto it = loaded_.find(normalizeFilename(rawFilename));
    return it != loaded_.end() ? it->second : nullptr;
}

void ObjectManager::destructObject(const std::shared_ptr<LpcObject>& obj,
                                    const std::function<void(const std::shared_ptr<LpcObject>&)>& onDestructed) {
    if (!obj || obj->isDestructed()) return;

    // simulate.c destruct_object(): if this is the base of a shadow
    // chain (ob->shadowed && !ob->shadowing), cascade-destruct the
    // entire chain then return.
    auto shadowedBy = obj->shadowedBy().lock();
    if (shadowedBy && !obj->shadowing().lock()) {
        auto top = shadowedBy;
        while (auto next = top->shadowedBy().lock()) top = next;
        while (top) {
            auto below = top->shadowing().lock();
            if (below) below->setShadowedBy(std::weak_ptr<LpcObject>());
            top->setShadowing(std::weak_ptr<LpcObject>());
            auto current = top;
            top = below;
            destructObject(current, onDestructed);
        }
        return;
    }
    // Otherwise splice obj out of the chain (doubly-linked unlink).
    if (auto shadowing = obj->shadowing().lock()) {
        shadowing->setShadowedBy(shadowedBy);
    }
    if (shadowedBy) {
        shadowedBy->setShadowing(obj->shadowing());
    }
    obj->setShadowing(std::weak_ptr<LpcObject>());
    obj->setShadowedBy(std::weak_ptr<LpcObject>());

    // simulate.c: close_referencing_sockets(ob) via callback (object
    // cannot depend on net).
    if (onDestructed) onDestructed(obj);

    // simulate.c: if this object was a snooper, clear the victim's
    // snooped_by. The snooped-by-interactive half is Connection::close().
    if (auto victim = obj->snooping().lock()) {
        victim->setSnoopedBy(std::weak_ptr<LpcObject>());
    }
    obj->setSnooping(std::weak_ptr<LpcObject>());

    obj->setDestructed(true);

    // object.c destruct2(): clear replaced_program (query_replaced_program).
    obj->setReplacedProgramName(std::nullopt);

    // simulate.c: remove_living_name(ob).
    LivingNameRegistry::remove(obj);

    // simulate.c obj_list unthread: drop from LiveObjectRegistry now.
    LiveObjectRegistry::remove(obj);

    // simulate.c: unlink from environment immediately.
    // Divergence: real contents-relocation (APPLY_MOVE cascade) is not
    // replicated; remaining inventory stays attached to the destructed
    // object.
    if (auto env = obj->environment().lock()) {
        auto& inv = env->inventory();
        inv.erase(std::remove(inv.begin(), inv.end(), obj), inv.end());
    }
    obj->setEnvironment(std::weak_ptr<LpcObject>());

    loaded_.erase(obj->filename());
    restoredObjects_.erase(std::remove(restoredObjects_.begin(), restoredObjects_.end(), obj),
                            restoredObjects_.end());
}

void ObjectManager::retainRestoredObjects(std::vector<std::shared_ptr<LpcObject>> objs) {
    restoredObjects_.insert(restoredObjects_.end(),
                             std::make_move_iterator(objs.begin()),
                             std::make_move_iterator(objs.end()));
}

ObjectManager::~ObjectManager() {
    // Drop retaining containers. Self-referential objects can still
    // pin themselves at refcount 1 until releaseAll() below.
    loaded_.clear();
    restoredObjects_.clear();
    programCache_.clear();
    programSource_.clear();
    compiling_.clear();
    virtualCompiling_.clear();
    master_.reset();
    simulEfunObject_.reset();

    // Last manager: break process-wide cycles so LpcObjects can drop.
    if (--g_liveManagerCount == 0) {
        LiveObjectRegistry::releaseAll();
    }
}

void ObjectManager::reloadObject(const std::shared_ptr<LpcObject>& obj,
                                  const std::function<void(const std::shared_ptr<LpcObject>&)>& onDestructed) {
    // Real "if (!obj->prog) return;". No swap concept here; no
    // isDestructed() guard either, matching reload_object().
    if (!obj) return;

    // Real: every object variable back to int 0 (const0u), not void.
    for (auto& v : obj->variables()) {
        v = Value(int64_t{0});
    }

    // Same two-branch shadow handling as destructObject(), except obj
    // itself is never destructed, only spliced out (or its shadows are).
    auto shadowedBy = obj->shadowedBy().lock();
    if (shadowedBy && !obj->shadowing().lock()) {
        auto cur = shadowedBy;
        while (cur) {
            auto next = cur->shadowedBy().lock();
            cur->setShadowing(std::weak_ptr<LpcObject>());
            cur->setShadowedBy(std::weak_ptr<LpcObject>());
            destructObject(cur, onDestructed);
            cur = next;
        }
    } else {
        if (auto shadowing = obj->shadowing().lock()) {
            shadowing->setShadowedBy(shadowedBy);
        }
        if (shadowedBy) {
            shadowedBy->setShadowing(obj->shadowing());
        }
    }
    obj->setShadowing(std::weak_ptr<LpcObject>());
    obj->setShadowedBy(std::weak_ptr<LpcObject>());

    // object.c: remove_living_name(obj).
    LivingNameRegistry::remove(obj);

    // call_create(): call___INIT then APPLY_CREATE. O_DESTRUCTED only
    // skips create(), not the initializer pass.
    if (vm_) {
        try {
            runObjectVarInitializers(obj, obj->program());
            if (!obj->isDestructed()) {
                vm_->callFunction(obj, "create", {});
            }
        } catch (const std::exception& e) {
            std::cerr << "[object] reload_object: create() failed for "
                       << obj->filename() << ": " << e.what() << "\n";
        }
    }
    // Re-arm reset/clean_up the same way a fresh load/clone does.
    // isClone() is left untouched: reload_object() does not change O_CLONE.
    if (!obj->isDestructed()) armResetAndCleanup(obj);
}

void ObjectManager::armResetAndCleanup(const std::shared_ptr<LpcObject>& obj) {
    if (!obj || obj->isDestructed()) return;
    // TIME_TO_RESET default, 1800 seconds.
    obj->armReset(std::chrono::seconds{1800});
    obj->setWillCleanUp(true);
}

} // namespace amlp

#include "amlp/efun/ParserPackage.hpp"
#include "amlp/core/Errors.hpp"
#include "amlp/object/LpcObject.hpp"
#include "amlp/vm/VM.hpp"
#include <algorithm>
#include <cctype>
#include <optional>

namespace amlp {

namespace {

struct TokenDef {
    const char* name;
    int token;
    bool modLegal;
};

// real "token_def_t tokens[]" (packages/parser.c). mod_legal matches
// real code exactly: only the four object-family tokens accept an
// "OBJ:lvpc"-style modifier suffix, STR/WRD never do.
constexpr TokenDef kTokenDefs[] = {
    {"OBJ", ParserToken::ObjA, true}, {"STR", ParserToken::Str, false}, {"WRD", ParserToken::Wrd, false},
    {"LIV", ParserToken::LivA, true}, {"OBS", ParserToken::Obs, true},  {"LVS", ParserToken::Lvs, true},
};

// real tokenize() (packages/parser.c). Returns std::nullopt at the end
// of the rule string (real tokenize()'s own "return 0" for "at the
// end"), advancing `pos` past whatever token was read. Throws for
// everything real tokenize() itself calls error() for.
std::optional<int> tokenizeOne(const std::string& rule, size_t& pos, const std::vector<std::string>& literals,
                                int& weight) {
    while (pos < rule.size() && rule[pos] == ' ') pos++;
    if (pos >= rule.size()) return std::nullopt;

    size_t start = pos;
    size_t sp = rule.find(' ', pos);
    size_t wordEnd = (sp == std::string::npos) ? rule.size() : sp;
    size_t wlen = wordEnd - start;
    pos = wordEnd;

    // real "if (n == 3 || (n > 4 && start[3] == ':'))" -- a bare 3-letter
    // token name, or a 3-letter name followed by ':' and at least one
    // modifier letter.
    if (wlen == 3 || (wlen > 4 && rule[start + 3] == ':')) {
        for (const auto& td : kTokenDefs) {
            if (rule.compare(start, 3, td.name) != 0) continue;
            int i = td.token;
            if (wlen != 3) {
                if (!td.modLegal) {
                    throw LpcRuntimeError(std::string("parser rule: illegal to have modifiers to '") + td.name +
                                           "'");
                }
                for (size_t k = start + 4; k < wordEnd; k++) {
                    switch (rule[k]) {
                        case 'l':
                            i |= ParserToken::LivModifier;
                            break;
                        case 'v':
                            i |= ParserToken::VisOnlyModifier;
                            break;
                        case 'p':
                            i |= ParserToken::PluralModifier;
                            break;
                        case 'c':
                            i |= ParserToken::ChooseModifier;
                            break;
                        default:
                            throw LpcRuntimeError(std::string("parser rule: unknown modifier '") + rule[k] + "'");
                    }
                }
            }
            // real weight bump: "switch(i) { default: /* some kind of
            // object */ weight += 2; ...; case STR_TOKEN: case
            // WRD_TOKEN: weight++; }" -- STR/WRD carry no modifiers, so
            // checking the two fixed values directly reproduces the
            // same switch exactly.
            if (i == ParserToken::Str || i == ParserToken::Wrd) {
                weight += 1;
            } else {
                weight += 2;
                if (i & ParserToken::PluralModifier) weight -= 1;
                if (i & ParserToken::LivModifier) weight += 1;
                if (!(i & ParserToken::VisOnlyModifier)) weight += 1;
            }
            return i;
        }
    }

    // real "(*weightp)++; /* must be a literal */" plus the literals[]
    // linear scan.
    weight += 1;
    std::string word = rule.substr(start, wlen);
    for (size_t li = 0; li < literals.size(); li++) {
        if (literals[li].size() == wlen && literals[li].compare(0, wlen, word) == 0) {
            return -(static_cast<int>(li) + 1);
        }
    }
    std::string shown = word.size() > 50 ? word.substr(0, 50) + "..." : word;
    throw LpcRuntimeError("parser rule: unknown token '" + shown + "'");
}

// Real parser.c's own process-wide `literals[]` global, populated by
// interrogate_master() (via f_parse_add_rule()) and read back later by
// rule_string() (via f_parse_dump()) with no re-fetch in between.
// Mirrored here the same way -- see ParserPackage::addRule()'s own
// comment for what this driver's own slice deliberately does not port
// from real interrogate_master() (the USERS/specials halves, both
// purely about sentence matching, not rule registration).
std::vector<std::string>& cachedLiterals() {
    static std::vector<std::string> literals;
    return literals;
}

} // namespace

std::vector<int> ParserPackage::tokenizeRule(const std::string& rule, const std::vector<std::string>& literals,
                                              int& weightOut) {
    std::vector<int> result;
    weightOut = 1;
    size_t pos = 0;
    int hasObj = 0;
    bool hasPlural = false;

    // real make_rule()'s own "while (idx < MAX_MATCHES) { ...; idx++; }
    // error(...)" loop, including its real off-by-one: a rule that uses
    // exactly all 10 slots always falls through to the "only 10 tokens
    // permitted" error below, even when the 10th token genuinely was
    // the last one in the string -- real code's loop-exit condition is
    // checked BEFORE it ever gets a chance to notice the input ran out
    // on that final token (the "no more input" check only happens on
    // the NEXT attempted token, a call this loop shape never reaches
    // once the count hits 10). Confirmed directly from source, not
    // silently "fixed": real mudlib rules are always far shorter than
    // 10 tokens (this driver's own corpus survey, STATUS.md's
    // 2026-08-18 entry, found nothing longer than 3), so this is
    // dead-letter in practice, faithfully kept rather than quietly
    // behaving differently than the real driver does.
    int idx = 0;
    while (idx < 10) {
        auto tok = tokenizeOne(rule, pos, literals, weightOut);
        if (!tok) return result;
        if (*tok >= ParserToken::ObjA) {
            if (++hasObj == 3) {
                throw LpcRuntimeError("parser rule: only two object tokens allowed per rule");
            }
            if (*tok & ParserToken::PluralModifier) {
                if (hasPlural) throw LpcRuntimeError("parser rule: only one plural token allowed per rule");
                hasPlural = true;
            }
        }
        result.push_back(*tok);
        idx++;
    }
    throw LpcRuntimeError("parser rule: only 10 tokens permitted per rule");
}

std::string ParserPackage::ruleString(const std::vector<int>& tokens, const std::vector<std::string>& literals) {
    std::string out;
    for (int rawTok : tokens) {
        int tok = rawTok & ~ParserToken::ChooseModifier;
        switch (tok) {
            case ParserToken::ObjA:
            case ParserToken::Obj:
                out += "OBJ ";
                break;
            case ParserToken::LivA:
            case ParserToken::Liv:
                out += "LIV ";
                break;
            case ParserToken::Obs:
            case ParserToken::Obs | ParserToken::VisOnlyModifier:
                out += "OBS ";
                break;
            case ParserToken::Lvs:
            case ParserToken::Lvs | ParserToken::VisOnlyModifier:
                out += "LVS ";
                break;
            case ParserToken::Str:
                out += "STR ";
                break;
            case ParserToken::Wrd:
                out += "WRD ";
                break;
            default:
                // real code: "default: p = strput(p, end,
                // literals[-(tok + 1)]);" -- `tok` there is the switch
                // expression's own side-effecting assignment ("switch
                // ((tok = vn->token[index++]) & ~CHOOSE_MODIFIER)"),
                // which still holds the RAW, unmasked token value in
                // C's own comma/assignment semantics; only the switch's
                // own selector went through the mask. Using the masked
                // `tok` here instead of `rawTok` would corrupt every
                // negative (literal) value: -1 & ~CHOOSE_MODIFIER is
                // -65, not -1, since a negative int's high bits are
                // already all 1s in two's complement. rawTok is the
                // correct real equivalent.
                if (rawTok <= 0) {
                    size_t li = static_cast<size_t>(-(rawTok + 1));
                    if (li < literals.size()) out += literals[li];
                    out += " ";
                }
                break;
        }
    }
    if (!out.empty()) out.pop_back(); // real rule_string(): "*(p-1) = 0; /* nuke last space */"
    return out;
}

std::unordered_map<std::string, std::vector<VerbEntry>>& ParserPackage::verbs() {
    static std::unordered_map<std::string, std::vector<VerbEntry>> table;
    return table;
}

namespace {
// real "if (verb_entry->match_name == verb && verb_entry->real_name ==
// verb && !(verb_entry->flags & VB_IS_SYN)) break;" -- the one shape
// every real plain (non-synonym) verb entry has: real_name==match_name
// ==name, isSynonym==false. Used by addRule() (find-or-create) and by
// addSynonym()'s own "oldVerb must already be a real verb" lookup
// (real code's separate `vb` search uses the same condition minus the
// name argument being the same on both sides, which is automatically
// true for a plain entry).
VerbEntry* findPlainEntry(std::vector<VerbEntry>& entries, const std::string& name) {
    for (auto& e : entries) {
        if (e.realName == name && e.matchName == name && !e.isSynonym) return &e;
    }
    return nullptr;
}

void fillLitFromTokens(VerbRuleNode& node) {
    // real "for (i = 0, j = 0; tokens[i]; i++) if (tokens[i] <= 0 && j <
    // 2) lit[j++] = -(tokens[i]+1);" -- first two literal-token indices,
    // scanning every token, not stopping the scan once both are found.
    for (int t : node.tokens) {
        if (t > 0) continue;
        int litIndex = -(t + 1);
        if (node.lit[0] == -1) {
            node.lit[0] = litIndex;
        } else if (node.lit[1] == -1) {
            node.lit[1] = litIndex;
        }
    }
}

// See VerbRuleNode::hasObjectToken's own comment: real VB_HAS_OBJ,
// computed per-node here instead of per-verb.
bool computeHasObjectToken(const std::vector<int>& tokens) {
    for (int t : tokens) {
        if (t >= ParserToken::ObjA) return true;
    }
    return false;
}

// See VerbRuleNode::objectTokenCount's own comment.
int computeObjectTokenCount(const std::vector<int>& tokens) {
    int count = 0;
    for (int t : tokens) {
        if (t >= ParserToken::ObjA) count++;
    }
    return count;
}

} // namespace

void ParserPackage::addRule(const std::string& verb, const std::string& rule,
                             const std::shared_ptr<LpcObject>& handler, const std::vector<std::string>& literals) {
    cachedLiterals() = literals;

    int weight = 0;
    std::vector<int> tokens = tokenizeRule(rule, cachedLiterals(), weight);

    auto& entries = verbs()[verb];
    VerbEntry* target = findPlainEntry(entries, verb);
    if (!target) {
        VerbEntry entry;
        entry.realName = verb;
        entry.matchName = verb;
        entries.push_back(std::move(entry));
        target = &entries.back();
    }

    VerbRuleNode node;
    node.tokens = tokens;
    node.weight = weight;
    node.handler = handler;
    node.hasObjectToken = computeHasObjectToken(tokens);
    node.objectTokenCount = computeObjectTokenCount(tokens);
    fillLitFromTokens(node);

    // real "verb_node->next = verb_entry->node; verb_entry->node =
    // verb_node;" -- prepend, newest first.
    target->nodes.insert(target->nodes.begin(), std::move(node));
}

void ParserPackage::addSynonym(const std::string& newVerb, const std::string& oldVerb, const std::string& rule,
                                const std::shared_ptr<LpcObject>& caller, const std::vector<std::string>& literals) {
    // real "if (old_verb == new_verb) error(\"Verb cannot be a synonym
    // for itself.\\n\");" -- a plain string-content comparison here;
    // real code's own version is a shared-string *pointer* comparison,
    // but shared strings guarantee unique interning, so the observable
    // condition (same text) is identical either way.
    if (newVerb == oldVerb) {
        throw LpcRuntimeError("parse_add_synonym: a verb cannot be a synonym for itself");
    }

    // real code's own `old_verb = SHARED_STRING(sp-1)` can come back
    // null purely from FluffOS's own global shared-string table never
    // having interned that exact text before (a real, but driver-
    // internal, quirk this driver has no equivalent of -- Value::string
    // is a plain std::string, no interning table at all), which real
    // code checks separately ("if (!old_verb) error(...)") before even
    // trying the verb lookup below. That separate check is redundant
    // with the lookup's own failure case for every real, observable
    // purpose (an unregistered name fails the same "is not a verb!"
    // error whichever check catches it first), so this driver only
    // needs the one lookup-failure check that actually matters here.
    // find(), not operator[]: a lookup miss must not leave a spurious
    // empty entry behind in the registry (operator[] would silently
    // default-construct one).
    VerbEntry* vb = nullptr;
    if (auto oldIt = verbs().find(oldVerb); oldIt != verbs().end()) {
        vb = findPlainEntry(oldIt->second, oldVerb);
    }
    if (!vb) {
        throw LpcRuntimeError("parse_add_synonym: '" + oldVerb + "' is not a verb");
    }

    auto& newEntries = verbs()[newVerb];
    bool wantSynonym = rule.empty();
    // real "if (verb_entry->real_name == new_verb && verb_entry->
    // match_name == old_verb) { if (rule) { if (!(flags & VB_IS_SYN))
    // break; } else { if (flags & VB_IS_SYN) break; } }" -- reuse an
    // existing entry only if it already has the right (rule-copy vs
    // alias) shape; otherwise a fresh one is created alongside it.
    VerbEntry* target = nullptr;
    for (auto& e : newEntries) {
        if (e.realName == newVerb && e.matchName == oldVerb && e.isSynonym == wantSynonym) {
            target = &e;
            break;
        }
    }
    if (!target) {
        VerbEntry entry;
        entry.realName = newVerb;
        // real code sets match_name to old_verb here unconditionally,
        // for BOTH forms -- even a freshly-created 3-arg rule-copy
        // entry's own match_name is old_verb, not new_verb, which is
        // why parse_dump() can print "Verb new_verb (old_verb):" for a
        // rule-copied verb, matching a real, if slightly surprising,
        // behavior (see VerbEntry's own class comment) rather than the
        // "real_name==match_name" shape a plain parse_add_rule()-created
        // entry always has.
        entry.matchName = oldVerb;
        newEntries.push_back(std::move(entry));
        target = &newEntries.back();
    }

    if (!rule.empty()) {
        cachedLiterals() = literals;
        int weight = 0;
        std::vector<int> tokens = tokenizeRule(rule, cachedLiterals(), weight);

        // real "for (vn = vb->node; vn; vn = vn->next) { for (i = 0;
        // tokens[i]; i++) if (vn->token[i] != tokens[i]) break; if
        // (!tokens[i] && !vn->token[i]) break; }" -- find the exact
        // rule (by full token-sequence equality) already registered
        // under oldVerb.
        VerbRuleNode* found = nullptr;
        for (auto& n : vb->nodes) {
            if (n.tokens == tokens) {
                found = &n;
                break;
            }
        }
        if (!found) {
            throw LpcRuntimeError("parse_add_synonym: no such rule defined under '" + oldVerb + "'");
        }
        auto foundHandler = found->handler.lock();
        if (foundHandler != caller) {
            throw LpcRuntimeError("parse_add_synonym: rule owned by a different object");
        }

        // real "memcpy(verb_node, vn, ...)" -- a full copy of the
        // matched node (same tokens/lit/weight/handler), prepended onto
        // the target entry, which stays non-synonym.
        VerbRuleNode copy = *found;
        target->nodes.insert(target->nodes.begin(), std::move(copy));
    } else {
        // real "syn->flags = VB_IS_SYN | ...; syn->real = vb;" -- pure
        // aliasing, no rule nodes of its own.
        target->isSynonym = true;
        target->synonymOf = vb->realName;
    }
}

void ParserPackage::removeRules(const std::string& verb, const std::shared_ptr<LpcObject>& handler) {
    auto it = verbs().find(verb);
    if (it == verbs().end()) return;
    // real f_parse_remove()'s own bucket walk only ever does real work
    // for entries where match_name==verb -- which, restricted to this
    // name's own entries, means exactly the non-synonym one(s) (a
    // synonym entry keyed by this name has match_name pointing at its
    // *target* verb instead, and has no `node` list of its own to walk
    // -- see this method's own header comment on real code's
    // accidental UB there, deliberately not replicated).
    for (auto& entry : it->second) {
        if (entry.isSynonym) continue;
        auto& nodes = entry.nodes;
        nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                                    [&](const VerbRuleNode& n) { return n.handler.lock() == handler; }),
                    nodes.end());
    }
}

std::string ParserPackage::dump() {
    auto& table = verbs();
    std::vector<std::string> names;
    names.reserve(table.size());
    for (const auto& [name, entries] : table) names.push_back(name);
    std::sort(names.begin(), names.end());

    std::string out;
    for (const auto& name : names) {
        for (const VerbEntry& v : table.at(name)) {
            if (v.realName == v.matchName) {
                out += "Verb " + v.realName + ":\n";
            } else {
                out += "Verb " + v.realName + " (" + v.matchName + "):\n";
            }
            if (v.isSynonym) {
                out += "  Synonym for: " + v.synonymOf + "\n";
                continue;
            }
            for (const auto& node : v.nodes) {
                auto handler = node.handler.lock();
                // This driver's own destruct() does not drop every
                // shared_ptr to a destructed object the moment it is
                // destructed (see LpcObject::isDestructed()'s own
                // comment: it keeps working as a plain C++ object until
                // the last reference actually goes away) -- so a
                // weak_ptr can still .lock() successfully here for an
                // object that is, for every real-LPC-visible purpose,
                // already gone. Treated the same as an expired
                // weak_ptr, matching the same "destructed reads back as
                // gone" convention this driver's own %O sprintf
                // formatter already applies to any object value
                // (EfunTable.cpp's own "if (!*ov ||
                // (*ov)->isDestructed()) return \"0\";").
                if (handler && handler->isDestructed()) handler.reset();
                // real "(/%s)" (f_parse_dump(): "vn->handler->obname"),
                // but this driver's own LpcObject::filename() already
                // stores the full leading-slash path for a genuinely
                // loaded/cloned object (confirmed against
                // ObjectManager::compile()'s own "config_.mudlibRoot()
                // + filename + \".c\"" concatenation, which requires
                // filename to already start with '/') -- not the bare,
                // slash-free obname real FluffOS keeps separately.
                // Prepending another '/' here would double it up, so
                // this uses filename() as-is.
                std::string handlerText = handler ? handler->filename() : "destructed";
                out += "  (" + handlerText + ") " + ruleString(node.tokens, cachedLiterals()) + "\n";
            }
        }
    }
    return out;
}

void ParserPackage::onObjectDestroyed(const std::shared_ptr<LpcObject>& destructed) {
    if (!destructed) return;
    // real "if (pinfo->flags & PI_VERB_HANDLER) { ... }" -- an object
    // that never successfully registered a rule (parse_add_rule()/
    // parse_add_synonym() always set this flag on success, see their
    // own EfunTable.cpp registrations) has nothing to unlink; skip the
    // full registry walk entirely, matching real code's own guard.
    if (!destructed->hasParseInfo() || !(destructed->parseInfoFlags() & ParserInfoFlag::VerbHandler)) {
        destructed->setHasParseInfo(false);
        destructed->setParseInfoFlags(0);
        return;
    }

    // real "for (i = 0; i < VERB_HASH_SIZE; i++) { verb_t *v = verbs[i];
    // while (v) { ... unlink every node whose handler == pinfo->ob ...
    // v = v->next; } }" -- every verb entry, every name, not scoped to
    // one verb the way removeRules() (parse_remove()'s own real
    // equivalent) is. Synonym entries have no nodes of their own in
    // this driver's representation (see VerbRuleNode's own comment), so
    // iterating them here is harmless, matching real code's own
    // accidental-but-inert behavior on a verb_syn_t there.
    for (auto& [name, entries] : verbs()) {
        for (auto& entry : entries) {
            auto& nodes = entry.nodes;
            nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                                        [&](const VerbRuleNode& n) { return n.handler.lock() == destructed; }),
                        nodes.end());
        }
    }

    // real remove_ids(pinfo) is a no-op here (nothing in this slice
    // populates the noun/adj/plural id cache it would free -- see
    // LpcObject::parseInfoFlags()'s own comment); real "FREE(pinfo);"
    // is this.
    destructed->setHasParseInfo(false);
    destructed->setParseInfoFlags(0);
}

const std::vector<std::string>& ParserPackage::currentLiterals() { return cachedLiterals(); }

// ============================================================================
// Noun-phrase-to-object resolution engine, pieces 1-4 (ROADMAP.md row
// 0.13a item 8): the real per-object noun/adjective/plural cache
// (interrogate_object()), the rest of interrogate_master() (the USERS/
// SPECIALS halves), environment-based object collection
// (rec_add_object()/find_uninited_objects()/add_objects_from_array()/
// get_objects_from_array()), and the word -> object-index hash table
// (add_hash_entry()/add_to_hash_table()) -- together, real load_objects()
// in full. Not yet called by parseSentence() itself: item 8 piece 5
// (parse_obj(), the real word-matching logic that would actually consume
// LoadedObjectSet) is the next slice. See LoadedObjectSet's own header
// comment for why this is a real, complete, independently testable
// pipeline on its own rather than a partial stand-in.
// ============================================================================

namespace {

constexpr size_t kMaxNumObjects = 1024; // real MAX_NUM_OBJECTS (packages/parser.h)
constexpr int kRaoInReach = 1;          // real RAO_INREACH
constexpr int kRaoMy = 2;               // real RAO_MY

// real parse_copy_array(): copies only the T_STRING elements of `v` (if
// it is even an array at all), silently skipping and compacting past
// any non-string element -- not every noun()/plural()/adjective() lfun
// in a real mudlib is guaranteed well-behaved.
std::vector<std::string> arrayOfStringsFrom(const Value& v) {
    std::vector<std::string> out;
    auto* arrPtr = std::get_if<std::shared_ptr<Array>>(&v.data);
    if (!arrPtr || !*arrPtr) return out;
    for (const Value& item : (*arrPtr)->items) {
        if (auto* s = std::get_if<std::string>(&item.data)) out.push_back(*s);
    }
    return out;
}

// real NEED_REFRESH(ob): "(ob->pinfo && ((ob->pinfo->flags &
// (PI_SETUP|PI_REFRESH)) != PI_SETUP))" -- note the real short-circuit:
// an object that never called parse_init() at all (no pinfo) is never
// "in need of refresh" by this macro's own definition, which is exactly
// why real rec_add_object()/add_objects_from_array() also gate on
// ob->pinfo separately before ever adding an object to the numbered
// universe at all -- an object invisible to the parser stays invisible
// to it, it is never silently auto-registered.
bool needRefresh(const std::shared_ptr<LpcObject>& ob) {
    if (!ob->hasParseInfo()) return false;
    int flags = ob->parseInfoFlags();
    return (flags & (ParserInfoFlag::Setup | ParserInfoFlag::Refresh)) != ParserInfoFlag::Setup;
}

// real find_uninited_objects() (packages/parser.c): the default (no
// explicit parse_env) discovery-pass environment walk, item 8 piece 3.
void findUninitedObjects(std::vector<std::shared_ptr<LpcObject>>& discovered, const std::shared_ptr<LpcObject>& ob) {
    if (!ob || ob->isDestructed()) return;
    if (needRefresh(ob)) {
        if (discovered.size() >= kMaxNumObjects) return;
        discovered.push_back(ob);
    }
    for (auto& child : ob->inventory()) findUninitedObjects(discovered, child);
}

// real get_objects_from_array() (packages/parser.c): the explicit-
// parse_env discovery-pass equivalent of findUninitedObjects() above,
// item 8 piece 3.
void getObjectsFromArray(std::vector<std::shared_ptr<LpcObject>>& discovered, const std::vector<Value>& arr) {
    for (const Value& item : arr) {
        if (auto* nestedPtr = std::get_if<std::shared_ptr<Array>>(&item.data); nestedPtr && *nestedPtr) {
            getObjectsFromArray(discovered, (*nestedPtr)->items);
        }
        auto* obPtr = std::get_if<std::shared_ptr<LpcObject>>(&item.data);
        if (!obPtr || !*obPtr || (*obPtr)->isDestructed()) continue;
        if (needRefresh(*obPtr)) {
            if (discovered.size() >= kMaxNumObjects) return;
            discovered.push_back(*obPtr);
        }
    }
}

// real init_users() (packages/parser.c): the discovery-pass half of
// interrogate_master()'s own USERS handling, item 8 piece 2 -- any
// master-user-list object that already has pinfo and needs refreshing.
void initUsersDiscovery(std::vector<std::shared_ptr<LpcObject>>& discovered, const std::vector<Value>& users) {
    for (const Value& item : users) {
        auto* obPtr = std::get_if<std::shared_ptr<LpcObject>>(&item.data);
        if (!obPtr || !*obPtr || !(*obPtr)->hasParseInfo()) continue;
        if (needRefresh(*obPtr)) {
            if (discovered.size() >= kMaxNumObjects) return;
            discovered.push_back(*obPtr);
        }
    }
}

// real rec_add_object() (packages/parser.c): the default (no explicit
// parse_env) object-index-assignment pass, item 8 piece 3. Walks
// `ob->inventory()` directly rather than reimplementing real code's own
// first_inv()/next_inv() pointer-chase -- this driver's own inventory_
// vector already preserves the same real traversal order, the same
// intrusive-list-to-container simplification already used elsewhere in
// this class (e.g. the verb registry's own hash-bucket-to-map collapse).
void recAddObject(LoadedObjectSet& result, std::vector<bool>& myObjects, const std::shared_ptr<LpcObject>& parseUser,
                   const std::shared_ptr<LpcObject>& ob, int flags) {
    if (!ob || ob->isDestructed()) return;
    if (ob->hasParseInfo()) {
        if (result.objects.size() >= kMaxNumObjects) return;
        size_t index = result.objects.size();
        if (flags & kRaoMy) {
            if (myObjects.size() <= index) myObjects.resize(index + 1);
            myObjects[index] = true;
        }
        if (ob == parseUser) {
            result.meObject = static_cast<int>(index);
            flags |= kRaoMy;
        }
        result.inReach.push_back((flags & kRaoInReach) != 0);
        result.objects.push_back(ob);
        int obFlags = ob->parseInfoFlags();
        if (!(obFlags & ParserInfoFlag::InvVisible)) return;
        if (!(obFlags & ParserInfoFlag::InvAccessible)) flags &= ~kRaoInReach;
    }
    for (auto& child : ob->inventory()) recAddObject(result, myObjects, parseUser, child, flags);
}

// real add_objects_from_array() (packages/parser.c): the explicit-
// parse_env object-index-assignment pass, item 8 piece 3. Unlike
// recAddObject() above, a nested array is only descended into when the
// immediately preceding (non-array) element's own PI_INV_VISIBLE flag
// was set -- real code's own caller-supplied "object, then its own
// contents as a nested array" convention -- and reaching parseUser here
// does not propagate RAO_MY onto that same element's own recorded
// flags, only onto a nested array immediately following it (real
// last_was_me, confirmed a genuine, deliberate asymmetry with
// rec_add_object() above, not a porting slip).
void addObjectsFromArray(LoadedObjectSet& result, std::vector<bool>& myObjects,
                          const std::shared_ptr<LpcObject>& parseUser, const std::vector<Value>& arr, int flags) {
    int lastFlags = 0;
    bool lastWasMe = false;
    for (const Value& item : arr) {
        if (auto* nestedPtr = std::get_if<std::shared_ptr<Array>>(&item.data); nestedPtr && *nestedPtr) {
            if (lastFlags & ParserInfoFlag::InvVisible) {
                int f = flags;
                if (!(lastFlags & ParserInfoFlag::InvAccessible)) f &= ~kRaoInReach;
                if (lastWasMe) f |= kRaoMy;
                addObjectsFromArray(result, myObjects, parseUser, (*nestedPtr)->items, f);
            }
        }
        lastFlags = 0;
        lastWasMe = false;
        auto* obPtr = std::get_if<std::shared_ptr<LpcObject>>(&item.data);
        if (!obPtr || !*obPtr || (*obPtr)->isDestructed()) continue;
        const auto& ob = *obPtr;
        if (!ob->hasParseInfo()) continue;
        if (result.objects.size() >= kMaxNumObjects) return;
        size_t index = result.objects.size();
        if (flags & kRaoMy) {
            if (myObjects.size() <= index) myObjects.resize(index + 1);
            myObjects[index] = true;
        }
        if (ob == parseUser) {
            result.meObject = static_cast<int>(index);
            lastWasMe = true;
        }
        result.inReach.push_back((flags & kRaoInReach) != 0);
        result.objects.push_back(ob);
        lastFlags = ob->parseInfoFlags();
    }
}

// real add_hash_entry() (packages/parser.c), item 8 piece 4: find or
// create `word`'s own HashEntry, its three bool vectors pre-sized to
// `objectCount` -- safe because, exactly like real code's own
// add_to_hash_table() and addNicknames() below (real add_nicknames(),
// its own only real caller), this only ever runs once the numbered
// object universe has already stopped growing. Real add_hash_entry()
// also has a second real caller, mark_hash_entry() (packages/parser.c:
// 1015-1037) -- confirmed genuine real dead code, not a port target:
// grepped the whole vendored driver tree, zero call sites anywhere for
// mark_hash_entry() itself (declared in packages/parser.h, defined,
// never invoked), the same "real code, never called" category already
// found for get_bb_uid()/multiple_adj()/err_obs() elsewhere in this
// row's own investigation.
HashEntry& addHashEntry(LoadedObjectSet& result, const std::string& word, size_t objectCount) {
    auto [it, inserted] = result.hashTable.try_emplace(word);
    if (inserted) {
        it->second.nounObjs.assign(objectCount, false);
        it->second.pluralObjs.assign(objectCount, false);
        it->second.adjObjs.assign(objectCount, false);
    }
    return it->second;
}

// real add_to_hash_table() (packages/parser.c), item 8 piece 4: folds
// object index `index`'s own cached noun/plural/adjective ids (piece 1)
// into the shared word hash table.
void addToHashTable(LoadedObjectSet& result, const std::shared_ptr<LpcObject>& ob, size_t index) {
    if (!ob->hasParseInfo()) return; // real "if (!pi) return;"
    for (const auto& id : ob->parseNounIds()) {
        HashEntry& he = addHashEntry(result, id, result.objects.size());
        he.isNoun = true;
        he.nounObjs[index] = true;
    }
    for (const auto& pl : ob->parsePluralIds()) {
        HashEntry& he = addHashEntry(result, pl, result.objects.size());
        he.isPlural = true;
        he.pluralObjs[index] = true;
    }
    for (const auto& adj : ob->parseAdjIds()) {
        HashEntry& he = addHashEntry(result, adj, result.objects.size());
        he.isAdj = true;
        he.adjObjs[index] = true;
    }
}

// real add_nicknames() (packages/parser.c:1095-1108), item 8 piece 4:
// "Note extremely clever delayed evaluation to avoid having to lookup
// object pointer -> index" (real code's own comment, kept for context --
// this is exactly why the real work is split into this eager half and
// expandNode()'s own lazy half below, rather than resolving every
// nickname to an object index up front). Real code walks `map`'s own
// mapping_node_t hash buckets directly and checks `mn->values[0].type
// == T_STRING` -- confirmed against real mapping.h/mapping.c directly
// (mapping_node_t::values[0] is the real KEY slot, not a value column;
// mapping.c:39's own "MAP_SVAL_HASH(mn->values[0])" hashes it for
// lookup, settling what could otherwise be read either way from
// add_nicknames() alone), i.e. "every string key in the mapping" -- this
// driver's own Mapping::entries already stores key/value as a
// std::pair, so `.first` is the direct equivalent, no ambiguity to
// resolve at this driver's own level. Only marks the flag; does not
// look at or validate the value at all (that is expandNode()'s own job,
// lazily, per hash entry, at most once).
void addNicknames(LoadedObjectSet& result, const Value& nicks) {
    auto* mapPtr = std::get_if<std::shared_ptr<Mapping>>(&nicks.data);
    if (!mapPtr || !*mapPtr) return;
    for (const auto& entry : (*mapPtr)->entries) {
        if (auto* key = std::get_if<std::string>(&entry.first.data)) {
            addHashEntry(result, *key, result.objects.size()).isNickname = true;
        }
    }
}

// real master_user_list (packages/parser.c) -- cached the same real way
// literals[] already is (cachedLiterals(), above). See
// ParserPackage::invalidateMasterUsersCache()'s own header comment
// (ParserPackage.hpp) for why this stays real process-wide state,
// invalidated only by an explicit parse_refresh() on master, rather than
// folding into LoadedObjectSet the way SentenceSession folded
// parse_sentence()'s own globals.
struct MasterUsersCache {
    bool valid = false;
    std::vector<Value> users; // real master_user_list's own item array, unfiltered
};
MasterUsersCache& masterUsersCache() {
    static MasterUsersCache cache;
    return cache;
}

// real interrogate_master()'s MS_HAS_USERS branch (packages/parser.c),
// item 8 piece 2.
const std::vector<Value>& fetchMasterUsers(VM& vm) {
    auto& cache = masterUsersCache();
    if (!cache.valid) {
        cache.users.clear();
        if (auto master = vm.masterObject()) {
            Value ret = vm.callFunction(master, "parse_command_users", {});
            if (auto* arrPtr = std::get_if<std::shared_ptr<Array>>(&ret.data); arrPtr && *arrPtr) {
                cache.users = (*arrPtr)->items;
            }
        }
        cache.valid = true;
    }
    return cache.users;
}

} // namespace

SpecialWordResult ParserPackage::checkSpecialWord(const std::string& word) {
    // real special_table[] (packages/parser.c's MS_HAS_SPECIALS branch)
    // -- see this method's own header comment (ParserPackage.hpp) for
    // why this is a plain static table rather than a cached/invalidated
    // one the way literals[]/master_user_list are.
    static const std::unordered_map<std::string, SpecialWordResult> kTable = {
        {"the", {SpecialWordKind::Article, 0}},
        {"me", {SpecialWordKind::Self, 0}},
        {"myself", {SpecialWordKind::Self, 0}},
        {"all", {SpecialWordKind::All, 0}},
        {"of", {SpecialWordKind::Of, 0}},
        {"and", {SpecialWordKind::And, 0}},
        {"a", {SpecialWordKind::Ordinal, 1}},
        {"an", {SpecialWordKind::Ordinal, 1}},
        {"any", {SpecialWordKind::Ordinal, 1}},
        {"first", {SpecialWordKind::Ordinal, 1}},
        {"second", {SpecialWordKind::Ordinal, 2}},
        {"other", {SpecialWordKind::Ordinal, 2}},
        {"third", {SpecialWordKind::Ordinal, 3}},
        {"fourth", {SpecialWordKind::Ordinal, 4}},
        {"fifth", {SpecialWordKind::Ordinal, 5}},
        {"sixth", {SpecialWordKind::Ordinal, 6}},
        {"seventh", {SpecialWordKind::Ordinal, 7}},
        {"eighth", {SpecialWordKind::Ordinal, 8}},
        {"ninth", {SpecialWordKind::Ordinal, 9}},
    };
    if (auto it = kTable.find(word); it != kTable.end()) return it->second;

    // real check_special_word()'s own numeric-ordinal fallback ("3rd",
    // "21st", ...): digits followed by exactly the right suffix for
    // that number, with real code's own "a teen is always 'th'" rule --
    // if the digit two before the end is '1', the suffix is always
    // "th" regardless of the last digit.
    if (!word.empty() && std::isdigit(static_cast<unsigned char>(word[0]))) {
        size_t digits = 0;
        while (digits < word.size() && std::isdigit(static_cast<unsigned char>(word[digits]))) digits++;
        if (digits > 0 && digits < word.size()) {
            long n = std::stol(word.substr(0, digits));
            std::string suffix = word.substr(digits);
            std::string ending = "th";
            if (!(digits >= 2 && word[digits - 2] == '1')) {
                switch (word[digits - 1]) {
                    case '1':
                        ending = "st";
                        break;
                    case '2':
                        ending = "nd";
                        break;
                    case '3':
                        ending = "rd";
                        break;
                }
            }
            if (suffix == ending) return SpecialWordResult{SpecialWordKind::Ordinal, n};
        }
    }
    return SpecialWordResult{};
}

void ParserPackage::interrogateObject(VM& vm, const std::shared_ptr<LpcObject>& ob) {
    // real "if (ob->pinfo->flags & PI_REFRESH) remove_ids(ob->pinfo);" --
    // a deliberate no-op here: C++ vectors self-manage the memory real
    // remove_ids() exists to free, and real remove_ids()'s own guard
    // ("if (pinfo->flags & PI_SETUP)") is provably always false at this
    // exact point in real code anyway -- real f_parse_refresh() clears
    // PI_SETUP in the same bitwise AND that sets PI_REFRESH ("pi->flags
    // &= PI_VERB_HANDLER; pi->flags |= PI_REFRESH;"), so by the time
    // interrogate_object() ever observes PI_REFRESH set, PI_SETUP is
    // already unset -- meaning real remove_ids() never actually frees
    // anything here either, confirmed directly from source, not assumed.
    int flags = ob->parseInfoFlags();
    if ((flags & ParserInfoFlag::Setup) && !(flags & ParserInfoFlag::Refresh)) {
        return; // real cache hit: "if (pinfo->flags & PI_SETUP && !(pinfo->flags & PI_REFRESH)) return;"
    }

    ob->setParseNounIds(arrayOfStringsFrom(vm.callFunction(ob, "parse_command_id_list", {})));
    if (ob->isDestructed()) return;

    // real "/* in case of an error */ ob->pinfo->flags |= PI_SETUP;
    // ob->pinfo->flags &= ~(PI_LIVING|PI_INV_ACCESSIBLE|PI_INV_VISIBLE);"
    // -- set right after the first apply succeeds, not at the end of
    // this function, so an object destructed by a later apply below
    // still ends up marked SETUP (avoiding it looking permanently
    // "not yet interrogated" to NEED_REFRESH() from here on).
    flags = ob->parseInfoFlags() | ParserInfoFlag::Setup;
    flags &= ~(ParserInfoFlag::Living | ParserInfoFlag::InvAccessible | ParserInfoFlag::InvVisible);
    ob->setParseInfoFlags(flags);

    ob->setParsePluralIds(arrayOfStringsFrom(vm.callFunction(ob, "parse_command_plural_id_list", {})));
    if (ob->isDestructed()) return;

    ob->setParseAdjIds(arrayOfStringsFrom(vm.callFunction(ob, "parse_command_adjectiv_id_list", {})));
    if (ob->isDestructed()) return;

    if (isTruthy(vm.callFunction(ob, "is_living", {}))) {
        ob->setParseInfoFlags(ob->parseInfoFlags() | ParserInfoFlag::Living);
    }
    if (ob->isDestructed()) return;

    if (isTruthy(vm.callFunction(ob, "inventory_accessible", {}))) {
        ob->setParseInfoFlags(ob->parseInfoFlags() | ParserInfoFlag::InvAccessible);
    }
    if (ob->isDestructed()) return;

    if (isTruthy(vm.callFunction(ob, "inventory_visible", {}))) {
        ob->setParseInfoFlags(ob->parseInfoFlags() | ParserInfoFlag::InvVisible);
    }
}

void ParserPackage::invalidateMasterUsersCache() { masterUsersCache().valid = false; }

LoadedObjectSet ParserPackage::loadObjects(VM& vm, const std::shared_ptr<LpcObject>& parseUser,
                                            const Value* envArray, const Value* nicks) {
    LoadedObjectSet result;

    // Step 1 (real load_objects()'s own comment: LPC code run during
    // interrogation can move objects, so the environment must not be
    // walked live while walking it for object-index-assignment
    // purposes -- discover first, interrogate, only then assign
    // indices in a second, separate pass below).
    std::vector<std::shared_ptr<LpcObject>> discovered;
    const std::vector<Value>* envItems = nullptr;
    if (envArray) {
        if (auto* arrPtr = std::get_if<std::shared_ptr<Array>>(&envArray->data); arrPtr && *arrPtr) {
            envItems = &(*arrPtr)->items;
        }
        if (envItems) getObjectsFromArray(discovered, *envItems);
    } else {
        if (!parseUser || parseUser->isDestructed()) {
            throw LpcRuntimeError("parse_sentence: no this_player() to parse from");
        }
        findUninitedObjects(discovered, parseUser->environment().lock());
    }

    const std::vector<Value>& masterUsers = fetchMasterUsers(vm);
    initUsersDiscovery(discovered, masterUsers);

    // Step 2: interrogate every discovered object (piece 1).
    for (auto& ob : discovered) interrogateObject(vm, ob);

    // Step 3: the real object-index-assignment pass.
    std::vector<bool> myObjects;
    if (envItems) {
        addObjectsFromArray(result, myObjects, parseUser, *envItems, kRaoInReach);
    } else {
        recAddObject(result, myObjects, parseUser, parseUser->environment().lock(), kRaoInReach);
    }

    // real "he = add_hash_entry(my_string); he->flags |= HV_ADJ;
    // bitvec_copy(&he->pv.adj, &my_objects);" -- the fixed "my"
    // adjective, covering exactly the objects the RAO_MY walk above
    // marked.
    {
        HashEntry& myEntry = addHashEntry(result, "my", result.objects.size());
        myEntry.isAdj = true;
        for (size_t i = 0; i < myObjects.size() && i < result.objects.size(); i++) {
            if (myObjects[i]) myEntry.adjObjs[i] = true;
        }
    }

    // real "if (parse_nicks) add_nicknames(parse_nicks);"
    // (packages/parser.c:1162-1163), the exact real position: right
    // after the fixed "my" adjective entry above, before the
    // "num_people" loop below. addNicknames()'s own comment has the
    // full real citation and the key-vs-value ambiguity it resolves.
    if (nicks) addNicknames(result, *nicks);

    // real load_objects()'s own final "num_people" loop, unconditional
    // regardless of whether an explicit parse_env was given (confirmed
    // directly -- real code has no "if (!parse_env)" guard around it,
    // and parse_user is always current_object for parse_sentence(),
    // never influenced by parse_env either -- see f_parse_sentence(),
    // packages/parser.c:3070, "parse_user = current_object;"
    // unconditionally): any master-user-list object not already
    // reachable within parseUser's own environment tree, provided their
    // own environment chain never crosses an inventory_visible()==false
    // link before (if ever) reaching parseUser's own environment.
    if (parseUser && !parseUser->isDestructed()) {
        auto parseUserEnv = parseUser->environment().lock();
        for (const Value& item : masterUsers) {
            auto* obPtr = std::get_if<std::shared_ptr<LpcObject>>(&item.data);
            if (!obPtr || !*obPtr || !(*obPtr)->hasParseInfo()) continue;
            const auto& ob = *obPtr;

            std::shared_ptr<LpcObject> env = ob;
            bool reachedParseUserEnv = false;
            while (env) {
                if (env == parseUserEnv) {
                    reachedParseUserEnv = true;
                    break;
                }
                env = env->environment().lock();
                if (env && env->hasParseInfo() && !(env->parseInfoFlags() & ParserInfoFlag::InvVisible)) {
                    env = nullptr;
                }
            }
            if (reachedParseUserEnv) continue;
            if (result.objects.size() >= kMaxNumObjects) break;
            result.inReach.push_back(true); // real "object_flags[num_objects + num_people] = 1;"
            result.objects.push_back(ob);
            result.numPeople++; // real "num_people++" (see LoadedObjectSet::numPeople's own comment)
        }
    }

    // Step 4 (piece 4): cur_livings/cur_accessible plus the word hash
    // table, all built in the same final index-ordered pass real
    // load_objects()'s own "for (i...) add_to_hash_table(...)" loop is.
    result.isLiving.assign(result.objects.size(), false);
    result.isAccessible.assign(result.objects.size(), false);
    for (size_t i = 0; i < result.objects.size(); i++) {
        addToHashTable(result, result.objects[i], i);
        if (result.objects[i]->parseInfoFlags() & ParserInfoFlag::Living) result.isLiving[i] = true;
        if (result.inReach[i]) result.isAccessible[i] = true;
    }

    return result;
}

// ============================================================================
// Sentence matching (real f_parse_sentence() and everything it calls:
// parse_sentence()/parse_recurse() (the sentence tokenizer, ROADMAP.md
// row 0.13a item 6), parse_rules()/parse_rule()/we_are_finished() (the
// recursive-descent matcher, item 9), check_functions()/process_answer()/
// make_function()/push_real_names()/do_the_call() (the can_/direct_/
// indirect_/do_ callback machinery, also item 9), and
// make_error_message()/get_the_error() (error reporting, item 10)).
//
// OBJ/LIV/OBS/LVS tokens now resolve real objects for any rule with at
// most one object-family token (parse_obj() itself, packages/parser.c:
// 1325-1543, plus the single-object slice of the can_/direct_/indirect_/
// do_ disambiguation family -- singular_check_functions()/
// plural_check_functions(), packages/parser.c:2029-2157 -- that turns a
// parse_obj() bitvec of candidates into one resolved object). A rule
// needing TWO object tokens (VerbRuleNode::objectTokenCount's own
// comment -- the real dependent_check_functions()/
// check_object_relations() ambiguity family, packages/parser.c:
// 2184-2493, deciding which direct/indirect object PAIR is jointly
// valid) is a separate, larger piece of real code, not attempted here;
// parseRulesFor() below skips any node with objectTokenCount >= 2
// entirely, rather than attempting and silently mismatching it.
// ============================================================================

namespace {

// real word_t (packages/parser.h). See the header's own note on why
// this stays private to this file. `rawStart`/`rawEnd` are byte offsets
// into the original (pre-lowercasing) input, spanning exactly the kept
// characters this word is made of -- provably equivalent to real code's
// own less precise `start`/`end` pointers once real strput_words()'s
// own leading/trailing-whitespace trim is accounted for (real code
// trims down to the same boundary this driver computes directly).
struct SentenceWord {
    std::string text;
    size_t rawStart = 0;
    size_t rawEnd = 0;
};

// real bitvec_t (packages/parser.h): a set of candidate object indices
// into the current parse's own LoadedObjectSet::objects. Represented as
// a plain std::vector<bool> rather than real code's own fixed-size
// MAX_NUM_OBJECTS-bit C array -- the same hash-bucket-chaining-to-map
// simplification already used elsewhere in this file (HashEntry's own
// nounObjs/pluralObjs/adjObjs, which this type is interchangeable
// with).
using ObjBitset = std::vector<bool>;

// real intersect(): bv1 &= bv2 in place, truncating bv1 down to
// min(bv1.size(), bv2.size()) first -- real bitvec_t's own ".last"
// truncation, faithfully reproduced (not just a defensive size guard):
// it is what makes the fixed "my" adjective entry (LoadedObjectSet's
// own header comment -- built from `my_objects`, sized BEFORE
// loadObjects()'s final "num_people" loop appends more objects) never
// match a person only reachable through that fallback loop, in real
// FluffOS included, not a porting artifact. Returns whether any bit
// survived the intersection.
bool bitsetIntersect(ObjBitset& a, const ObjBitset& b) {
    size_t n = std::min(a.size(), b.size());
    a.resize(n);
    bool found = false;
    for (size_t i = 0; i < n; i++) {
        bool v = static_cast<bool>(a[i]) && static_cast<bool>(b[i]);
        a[i] = v;
        if (v) found = true;
    }
    return found;
}

// real bitvec_count().
size_t bitsetCount(const ObjBitset& a) {
    size_t n = 0;
    for (bool b : a) {
        if (b) n++;
    }
    return n;
}

// real get_single(): the one set index, or -1 if none or more than one.
int bitsetSingle(const ObjBitset& a) {
    int found = -1;
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i]) {
            if (found != -1) return -1;
            found = static_cast<int>(i);
        }
    }
    return found;
}

// real match_t (packages/parser.h): `obs`/`number` are real
// match_t::val.obs/val.number (the candidate bitvec parse_obj() builds,
// and the single object it eventually resolves to -- see
// singularCheckFunctions()'s own comment for why these are two separate
// fields, not a union, matching real code exactly), `ordinal` is real
// match_t::ordinal (nonzero only for an explicit "the Nth X" match).
struct SentenceMatch {
    int token = 0;
    int first = 0;
    int last = 0;
    int ordinal = 0;
    ObjBitset obs;
    int number = -1;
};

// real parser_error_t (a tagged union in real code, parser_error_u):
// every reachable payload shape collapsed into one flat struct, only
// the fields matching `errorType` are ever populated/read. `nounName`/
// `nounIsPlural` back IsNot/NotLiving/NotAccessible (real err.noun,
// reduced to the two derived values get_the_error() actually reads off
// it -- err.noun->name and get_single(&err.noun->pv.noun)==-1 -- rather
// than storing a live HashEntry* the way real code does, since nothing
// else about the noun is ever consulted). `ambigObs` backs Ambig (real
// err.obs). `ordError` backs Ordinal (real err.ord_error).
// `thereIsNoText` backs ThereIsNo (real err.str_problem, resolved to its
// own text eagerly here rather than as raw word indices resolved later
// -- a legitimate simplification, same category as nounName/
// nounIsPlural above: nothing distinguishes the two once computed).
// `allocatedMessage` backs Allocated (real err.str). BadMultiple carries
// no payload at all, matching real code exactly.
struct ParserErrorInfo {
    int errorType = ParserErrorType::None;
    std::string allocatedMessage;
    std::string nounName;
    bool nounIsPlural = false;
    ObjBitset ambigObs;
    int ordError = 0;
    std::string thereIsNoText;
};

// real saved_error_t (packages/parser.c): one rejected plural-match
// candidate's own error, remembered by save_last_parallel_error() so a
// later do_-call array (push_bitvec_as_array(), errors_too=1) can report
// it alongside the accepted objects. `obj` is the candidate's own index
// into LoadedObjectSet::objects (real saved_error_t::obj), or -1 (real
// get_the_error()'s own "push_undefined()" case is never reached through
// this path, since save_last_parallel_error() always records a real
// object index -- listed as -1 only for parity with getTheError()'s own
// top-level -1 convention, ParserPackage.hpp's own comment).
struct SavedError {
    ParserErrorInfo err;
    int obj = -1;
};

// real parse_result_t -- one already-resolved winning match, cached by
// we_are_finished() and invoked later by do_the_call(). See
// ParserPackage.hpp's own removed comment (now here, since this type
// moved): all four res[i] use the real "do_" prefix; the index selects
// one of four real naming *strategies* for the same call (real
// make_function()'s own "try" parameter).
struct SentenceMatchResult {
    std::weak_ptr<LpcObject> handler;
    struct FunctionCall {
        std::string functionName;
        std::vector<Value> args;
    };
    FunctionCall res[4];
};

// real parse_state_t (packages/parser.h): the per-recursion-branch state
// real parse_rule() threads through as a pointer, copied by value
// ("local_state = *state;") at every point real code starts a genuinely
// new branch. Passed by reference here for the same "mutate in place,
// tail-recurse" steps real code's own pointer aliasing achieves; a local
// `MatchState` copy is made explicitly, matching real code exactly,
// wherever real code copies `*state` into a `local_state`.
struct MatchState {
    int tokIndex = 0;
    int wordIndex = 0;
    int numMatches = 0;
    int numErrors = 0;
    int numObjs = 0; // real num_objs: how many object-family matches this branch has committed to so far
};

// real parser.c's own single shared set of "current parse in progress"
// globals (num_words/words[]/matches[]/parse_vn/parse_verb_entry/
// best_match/best_error_match/best_num_errors/found_level/
// current_error_info/best_error_info/best_result/parse_user), collected
// into one object constructed fresh per parseSentence() call and
// threaded through by reference instead. A deliberate, real behavior
// improvement over real code's own global mutable statics -- real
// parse_sentence()'s own recursion guard is explicitly disabled ("may
// not be done in case of an error, or in case of tail recursion", real
// code's own comment), meaning a genuinely reentrant call in real
// FluffOS can silently corrupt an outer parse still in progress; this
// shape cannot do that, since each call gets its own session with no
// shared mutable global at all. Not a fidelity loss: nothing any real
// caller depends on requires the corruption itself, only each call's
// own correct result, and single-call behavior is identical either way.
struct SentenceSession {
    std::string rawInput;
    std::shared_ptr<LpcObject> caller; // real parse_user

    std::vector<SentenceWord> words;
    std::vector<SentenceMatch> matches; // real matches[], reused/overwritten via the same add_match() high-water-mark technique

    const VerbEntry* matchedVerb = nullptr; // real parse_verb_entry
    const VerbRuleNode* currentNode = nullptr; // real parse_vn

    int bestMatchWeight = 0;   // real best_match
    int bestErrorWeight = 0;   // real best_error_match
    int bestNumErrors = 5732;  // real best_num_errors -- "Yes. Exactly 5,732 errors. Don't ask." (reset_error()'s own real comment, kept verbatim)
    int foundLevel = 0;        // real found_level

    ParserErrorInfo currentError; // real current_error_info
    ParserErrorInfo bestError;    // real best_error_info

    std::optional<SentenceMatchResult> bestResult; // real best_result

    // real objects_loaded/loaded_objects[]/hash_table[]/etc -- the whole
    // noun-phrase-to-object universe this session's own ParserPackage::
    // loadObjects() call built (item 8), populated lazily the first time
    // a matched verb has any rule with an object-family token (real "if
    // (!objects_loaded && (parse_verb_entry->flags & VB_HAS_OBJ))
    // load_objects();", ParserPackage::parseSentence()'s own comment).
    LoadedObjectSet loaded;
    bool objectsLoaded = false;

    // real parallel_error_info/second_parallel_error_info/
    // parallel_errors -- scratch state for the per-candidate can_/
    // direct_/indirect_/do_ disambiguation family (singularCheckFunctions()/
    // pluralCheckFunctions()/parallelCheckFunctions(), all below).
    // parallelErrors is real code's own singly-linked list, represented
    // here as a vector with saveLastParallelError() inserting at the
    // front to match real code's own prepend order exactly (read by
    // pushBitvecAsArray() in that same head-first order).
    ParserErrorInfo parallelError;
    ParserErrorInfo secondParallelError;
    std::vector<SavedError> parallelErrors;

    // real direct_object/indirect_object (packages/parser.c): the
    // currently-fixed candidate PAIR checkObjectRelations() below is
    // testing right now, read back only by makeFunction()'s own
    // `which >= 4` branch (checkOneRelation()'s "is this ONE fixed pair
    // jointly valid" probe -- the only real call site that ever reads
    // either global instead of a match's own candidate bitvec). Real
    // code stores these as process-wide globals; collapsed into
    // per-session scratch fields here for the same reason every other
    // "current parse in progress" global already is (this struct's own
    // header comment) -- both are always set immediately before being
    // read, entirely within checkObjectRelations()'s own call, so
    // per-session scope loses nothing real code's own actual usage
    // pattern depends on.
    int directObject = -1;
    int indirectObject = -1;

    // real `parse_env` (packages/parser.c): the explicit object-array
    // override to parse_sentence()'s own third argument, when given --
    // loadObjects() below already fully implements consuming it
    // (getObjectsFromArray()/addObjectsFromArray(), item 8's own
    // pieces 3/4), this is purely the per-session carrier so
    // runParseMatch()'s own loadObjects() call site can pass it through.
    // Real code stores this as a process-wide static, reset per call via
    // free_parse_globals(); collapsed into a per-session field for the
    // same reason every other "current parse in progress" global already
    // is (this struct's own header comment).
    std::optional<Value> envArray;

    // real `parse_nicks` (packages/parser.c): same shape and same
    // reasoning as envArray directly above, one real difference worth
    // recording rather than assuming away -- real free_parse_globals()
    // (parser.c:621-639) explicitly resets `parse_nicks = 0;` (and
    // `parse_env = 0;`) after every single parse_sentence() call,
    // confirmed by reading it directly, so real code's own *observable*
    // contract already is "fresh per call, nothing leaks across calls,"
    // the same guarantee this driver's own fresh-per-call SentenceSession
    // gives for free -- not a coincidental simplification, the actual
    // real behavior. Only ParserPackage::parseSentence() ever populates
    // this (real f_parse_sentence()'s own "if (st_num_arg == 4)
    // parse_nicks = ...;" is the only real site that ever assigns
    // parse_nicks at all); parseMyRules() below leaves it unset, matching
    // real f_parse_my_rules() -- it has no `nicks` argument of its own,
    // and parse_nicks is unconditionally 0 by the time it would run
    // (freed after the prior call, never set by this call), so real
    // parse_my_rules() never resolves a nickname either.
    std::optional<Value> nicks;
};

// real isignore(x) = (!uisprint(x) || x == '\'').
bool isIgnorableChar(unsigned char c) { return !std::isprint(c) || c == '\''; }
// real iskeep(x) = uisalnum(x) || x == '*'.
bool isKeepChar(unsigned char c) { return std::isalnum(c) || c == '*'; }

// real parse_sentence()'s own word-splitting front end (packages/
// parser.c) -- item 6, the sentence tokenizer. Ported to byte offsets
// into a std::string rather than raw char* pointer arithmetic; see
// SentenceWord's own comment for why the resulting rawStart/rawEnd are
// provably equivalent to real code's own less-precise pointers. Real
// MAX_WORD_LENGTH/MAX_WORDS_PER_LINE truncation (pure C buffer-safety
// limits) is not ported -- no realistic input reaches them, and this
// driver's own containers have no equivalent fixed-size hazard to guard
// against.
std::vector<SentenceWord> splitSentenceWords(const std::string& input) {
    std::vector<SentenceWord> words;
    size_t n = input.size();
    size_t i = 0;

    std::string current;
    size_t wordRawStart = 0;
    size_t wordRawEnd = 0;
    bool haveWord = false;

    auto finishWord = [&]() {
        if (haveWord) {
            words.push_back(SentenceWord{current, wordRawStart, wordRawEnd});
            current.clear();
            haveWord = false;
        }
    };

    while (i < n) {
        unsigned char c = static_cast<unsigned char>(input[i]);
        if (isIgnorableChar(c)) {
            i++;
            continue;
        }
        if (isKeepChar(c)) {
            unsigned char lower = std::isupper(c) ? static_cast<unsigned char>(std::tolower(c)) : c;
            if (!haveWord) {
                haveWord = true;
                wordRawStart = i;
            }
            current += static_cast<char>(lower);
            wordRawEnd = i;
            i++;
            continue;
        }
        // separator: whitespace, or a run of other punctuation -- real
        // code's own "!iskeep(*inp) && !uisspace(*inp)" skip-run does
        // not special-case isignore() chars within it either, so
        // neither does this.
        finishWord();
        if (std::isspace(c)) {
            i++;
            while (i < n && std::isspace(static_cast<unsigned char>(input[i]))) i++;
        } else {
            while (i < n) {
                unsigned char cc = static_cast<unsigned char>(input[i]);
                if (isKeepChar(cc) || std::isspace(cc)) break;
                i++;
            }
        }
    }
    finishWord();
    return words;
}

// real strput_words() (packages/parser.c): the ORIGINAL-cased,
// original-punctuated text spanning word[first..last] inclusive,
// trimmed of leading/trailing whitespace -- used for a STR/WRD token's
// own matched text, deliberately distinct from the lowercased/stripped
// form used for grammar matching itself. Everything *between*
// session.words[first] and session.words[last] (including any
// punctuation the grammar-matching pass silently ignored) is preserved
// verbatim, since this operates on `session.rawInput` directly rather
// than reassembling from the individual (already-normalized) word
// strings.
std::string extractWordRange(const SentenceSession& session, int first, int last) {
    if (first < 0 || last < first || last >= static_cast<int>(session.words.size())) return "";
    size_t start = session.words[first].rawStart;
    size_t end = session.words[last].rawEnd;
    const std::string& raw = session.rawInput;
    while (start <= end && start < raw.size() && std::isspace(static_cast<unsigned char>(raw[start]))) start++;
    while (end > start && end < raw.size() && std::isspace(static_cast<unsigned char>(raw[end]))) end--;
    if (start > end || start >= raw.size()) return "";
    return raw.substr(start, end - start + 1);
}

int tokenAt(const VerbRuleNode& node, int index) {
    return index >= 0 && index < static_cast<int>(node.tokens.size()) ? node.tokens[index] : 0;
}

// real add_match() (packages/parser.c): appends to (or, past the first
// pass through this depth, overwrites) session.matches at
// state.numMatches, the same shared-array-reused-across-sibling-
// branches technique real code's own fixed `matches[]` array uses, via
// a vector instead. Real code's own "if (token == ERROR_TOKEN)
// state->num_errors++;" is folded in here too. Returns a reference to
// the new match so callers needing to fill in its own obs/ordinal
// (parseObj(), below) can do so directly, matching real code's own
// "mp = add_match(...); mp->val.obs = ...;" idiom.
SentenceMatch& addMatch(SentenceSession& session, MatchState& state, int token, int first, int last) {
    SentenceMatch fresh{};
    fresh.token = token;
    fresh.first = first;
    fresh.last = last;
    SentenceMatch* slot;
    if (state.numMatches < static_cast<int>(session.matches.size())) {
        session.matches[state.numMatches] = std::move(fresh);
        slot = &session.matches[state.numMatches];
    } else {
        session.matches.push_back(std::move(fresh));
        slot = &session.matches.back();
    }
    if (token == ParserToken::Error) state.numErrors++;
    state.numMatches++;
    return *slot;
}

// real check_literal() (packages/parser.c): real parse_rules()'s own
// fast prefilter, confirming literal `literals[litIndex]` appears
// somewhere at or after word index `start`. Returns the word index
// right after the match (so a second prefilter literal must appear
// strictly later), or 0 if not found.
int checkLiteral(const SentenceSession& session, int litIndex, int start) {
    const auto& literals = ParserPackage::currentLiterals();
    if (litIndex < 0 || static_cast<size_t>(litIndex) >= literals.size()) return 0;
    const std::string& want = literals[litIndex];
    for (int i = start; i < static_cast<int>(session.words.size()); i++) {
        if (session.words[i].text == want) return i + 1;
    }
    return 0;
}

void weAreFinished(VM& vm, SentenceSession& session, MatchState& state);
void parseObj(VM& vm, SentenceSession& session, MatchState& state, int tok, int ordinal);
void parseRule(VM& vm, SentenceSession& session, MatchState& state);

// real all_objects() (packages/parser.c): the full candidate universe
// parse_obj() starts from -- every loaded object, EXCEPT the trailing
// LoadedObjectSet::numPeople entries (real load_objects()'s own final
// "num_people" fallback loop) unless the currently-attempted rule's own
// registering object has PI_REMOTE_LIVINGS set.
ObjBitset allObjectsBitset(const SentenceSession& session) {
    size_t total = session.loaded.objects.size();
    bool remote = false;
    if (auto handler = session.currentNode->handler.lock()) {
        remote = (handler->parseInfoFlags() & ParserInfoFlag::RemoteLivings) != 0;
    }
    size_t visible = remote ? total : (total >= session.loaded.numPeople ? total - session.loaded.numPeople : 0);
    ObjBitset bv(total, false);
    for (size_t i = 0; i < visible; i++) bv[i] = true;
    return bv;
}

// real query_the_short() (packages/parser.c): an object's own "the_short"
// apply -- a mudlib convention, not a documented efun/apply -- falling
// back to "the thing" when destructed, undefined, or non-string.
std::string queryTheShort(VM& vm, const std::shared_ptr<LpcObject>& ob) {
    if (!ob || ob->isDestructed() || !vm.functionExists(ob, "the_short")) return "the thing";
    Value ret = vm.callFunction(ob, "the_short", {});
    if (auto* s = std::get_if<std::string>(&ret.data)) return *s;
    return "the thing";
}

// real expand_node() (packages/parser.c:1302-1323), ROADMAP.md row
// 0.13a's own "nicks" note, implemented 2026-08-20: a nickname hash
// entry's own one-shot lazy resolution, run from parseObj()'s own word
// loop below the first (and only the first) time it actually encounters
// a hash entry with isNickname still set. Two real quirks confirmed
// directly from the C and ported faithfully, not smoothed over:
//
// 1. "he->flags &= ~HV_NICKNAME;" is the function's very first
//    statement, unconditional -- it runs before any of the lookup/
//    validity checks below, so a *failed* resolution (destructed
//    object, an object never reached by this call's own loadObjects()
//    walk) still permanently disables re-attempting this same hash
//    entry for the rest of this call, exactly like a successful one.
//    If a nickname word appears twice in one sentence and the first
//    occurrence fails to resolve, the second occurrence does not get a
//    second attempt -- it just sees isNoun still false and falls
//    through, the same real outcome either way.
// 2. real "sv = find_string_in_mapping(parse_nicks, he->name); if
//    (sv->type != T_OBJECT) return;" -- find_string_in_mapping() (real
//    mapping.c:830-848) never returns null, it returns "&const0u" (a
//    static T_NUMBER 0) on a missing key, so a missing key and a
//    present-but-non-object value both fold into the exact same
//    "sv->type != T_OBJECT" bail, one check covering both real cases.
//    This driver's own Mapping::entries has no equivalent sentinel to
//    reuse, so the equivalent is written out as the two real cases it
//    actually is: "not found in entries" and "found but not an object",
//    same combined effect.
//
// The final real step -- "linear, but we only do this once per nickname
// they use" -- confirms the object must already be part of *this call's
// own* LoadedObjectSet::objects (built once, upfront, by loadObjects()
// before parseObj() ever runs): a nickname mapped to some real, live,
// non-destructed object this call's own environment/inventory walk
// simply never reached (a real, correctly-unresolved "object not yet
// loaded" case, not a bug) does not resolve, silently, exactly like any
// other failed candidate lookup elsewhere in this file.
void expandNode(SentenceSession& session, HashEntry& he, const std::string& word) {
    he.isNickname = false; // unconditional, see quirk 1 above -- runs even on every early return below
    if (!session.nicks) return;
    auto* mapPtr = std::get_if<std::shared_ptr<Mapping>>(&session.nicks->data);
    if (!mapPtr || !*mapPtr) return;

    // real find_string_in_mapping(parse_nicks, he->name) -- see quirk 2
    // above for why "not found" and "found but not T_OBJECT" collapse
    // into the same outcome here, matching real code's own single check.
    const Value* value = nullptr;
    for (const auto& entry : (*mapPtr)->entries) {
        if (auto* key = std::get_if<std::string>(&entry.first.data); key && *key == word) {
            value = &entry.second;
            break;
        }
    }
    if (!value) return; // real "sv->type != T_OBJECT" via the missing-key half of find_string_in_mapping()'s own const0u fallback
    auto* obPtr = std::get_if<std::shared_ptr<LpcObject>>(&value->data);
    if (!obPtr || !*obPtr) return; // real "sv->type != T_OBJECT" via the wrong-value-type half
    const auto& ob = *obPtr;
    if (ob->isDestructed()) return; // real "if (ob->flags & O_DESTRUCTED) return;"
    if (!ob->hasParseInfo()) return; // real "if (ob->pinfo == 0) return;"

    for (size_t i = 0; i < session.loaded.objects.size(); i++) {
        if (session.loaded.objects[i] == ob) {
            he.isNoun = true;
            he.nounObjs[i] = true;
            return;
        }
    }
    // real: falls off the end of the loop silently -- ob is a real,
    // live, interrogated object, just not one this call's own
    // loadObjects() ever reached, so no match, exactly like real code.
}

// real parse_obj() (packages/parser.c:1325-1543), ROADMAP.md row 0.13a
// item 8 piece 5 -- the real noun-phrase word-matching engine: articles
// ("the"), "all"/"all of", possessive "my", ordinals ("the second
// sword", numeric "3rd"), nicknames (expand_node(), real and tested as
// of 2026-08-20, see its own comment directly above), adjective chains
// ("big red sword" -- each adjective narrows `objects` before the noun
// itself is read), singular-vs-plural noun matching (OBJ vs OBS), and
// the LIV_MODIFIER/VIS_ONLY_MODIFIER filters, all via bitvector
// intersection against LoadedObjectSet's own hash table. `state` is
// mutated in place as words are consumed, exactly matching real code's
// own "parse_state_t *state" pointer aliasing -- each individual
// candidate interpretation gets its own `localState` copy to recurse
// into parseRule() with, leaving `state` itself free to keep advancing
// through an adjective chain.
//
// real code's own `err_obs`/`multiple_adj` locals are write-only dead
// code (confirmed directly: neither is ever read anywhere in
// packages/parser.c) and are not ported at all.
void parseObj(VM& vm, SentenceSession& session, MatchState& state, int tok, int ordinal) {
    int start = state.wordIndex;
    ObjBitset objects = allObjectsBitset(session);
    bool ordLegal = (ordinal == 0);
    bool singularLegal = true;
    const int numWords = static_cast<int>(session.words.size());

    while (true) {
        if (state.wordIndex == numWords) return;
        std::string str = session.words[state.wordIndex++].text;

        SpecialWordResult special = ParserPackage::checkSpecialWord(str);
        switch (special.kind) {
            case SpecialWordKind::Article:
                continue; // real "case SW_ARTICLE: continue;"
            case SpecialWordKind::All: {
                singularLegal = false;
                if (state.wordIndex < numWords &&
                    ParserPackage::checkSpecialWord(session.words[state.wordIndex].text).kind ==
                        SpecialWordKind::Of) {
                    state.wordIndex++;
                    continue;
                }
                MatchState localState = state;
                if (tok & ParserToken::PluralModifier) {
                    localState.numObjs++;
                    SentenceMatch& m = addMatch(session, localState, tok, start, state.wordIndex - 1);
                    m.obs = objects;
                    m.ordinal = 0;
                } else {
                    session.currentError = ParserErrorInfo{};
                    session.currentError.errorType = ParserErrorType::BadMultiple;
                    addMatch(session, localState, ParserToken::Error, start, state.wordIndex - 1);
                }
                parseRule(vm, session, localState);
                break;
            }
            case SpecialWordKind::Self:
                if (session.loaded.meObject != -1) {
                    MatchState localState = state;
                    localState.numObjs++;
                    SentenceMatch& m = addMatch(session, localState, tok, start, state.wordIndex - 1);
                    m.obs.assign(session.loaded.objects.size(), false);
                    m.obs[static_cast<size_t>(session.loaded.meObject)] = true;
                    m.ordinal = 0;
                    parseRule(vm, session, localState);
                }
                break;
            case SpecialWordKind::Ordinal:
                if (ordLegal) {
                    MatchState localState = state;
                    parseObj(vm, session, localState, tok, static_cast<int>(special.arg));
                }
                break;
            default:
                break;
        }

        // real "if (str != my_string) ord_legal = 0;" -- a documented
        // hack allowing "my first sword"/"my 1st red sword" (the
        // possessive itself never disqualifies a following ordinal).
        if (str != "my") ordLegal = false;

        auto hnodeIt = session.loaded.hashTable.find(str);
        if (hnodeIt == session.loaded.hashTable.end()) return; // real "if (!hnode) break;"
        HashEntry& hnode = hnodeIt->second;

        // real "if (hnode->flags & HV_NICKNAME) expand_node(hnode);" --
        // real position, right here, before the isNoun check just below
        // gets a chance to read whatever expandNode() may have just set.
        if (hnode.isNickname) expandNode(session, hnode, str);

        if (singularLegal && hnode.isNoun) {
            bool exploreErrors = (session.bestMatchWeight == 0) && (state.numErrors < session.bestNumErrors);
            MatchState localState = state;
            ObjBitset saveObs = objects;
            int errorType = ParserErrorType::None;
            bool skip = false;

            if (!bitsetIntersect(objects, hnode.nounObjs)) {
                if (!exploreErrors) skip = true;
                else errorType = ParserErrorType::IsNot;
            } else if ((tok & ParserToken::LivModifier) && !bitsetIntersect(objects, session.loaded.isLiving)) {
                if (!exploreErrors) skip = true;
                else errorType = ParserErrorType::NotLiving;
            } else if (!(tok & ParserToken::VisOnlyModifier) &&
                       !bitsetIntersect(objects, session.loaded.isAccessible)) {
                if (!exploreErrors) skip = true;
                else errorType = ParserErrorType::NotAccessible;
            }

            if (!skip) {
                if (errorType != ParserErrorType::None) {
                    session.currentError = ParserErrorInfo{};
                    session.currentError.errorType = errorType;
                    session.currentError.nounName = str;
                    session.currentError.nounIsPlural = (bitsetSingle(hnode.nounObjs) == -1);
                    addMatch(session, localState, ParserToken::Error, start, state.wordIndex - 1);
                } else {
                    SentenceMatch& m =
                        addMatch(session, localState, tok & ~ParserToken::PluralModifier, start, state.wordIndex - 1);
                    m.obs = objects;
                    m.ordinal = ordinal;
                    localState.numObjs++;
                }
                parseRule(vm, session, localState);
            }
            objects = saveObs;
        }

        if (ordinal == 0 && hnode.isPlural) {
            bool exploreErrors = (session.bestMatchWeight == 0) && (state.numErrors < session.bestNumErrors);
            MatchState localState = state;
            ObjBitset saveObs = objects;
            int errorType = ParserErrorType::None;
            bool skip = false;
            bool isBadMultiple = false;

            if (!(tok & ParserToken::PluralModifier)) {
                if (!exploreErrors) skip = true;
                else {
                    errorType = ParserErrorType::BadMultiple;
                    isBadMultiple = true;
                }
            } else if (!bitsetIntersect(objects, hnode.pluralObjs)) {
                if (!exploreErrors) skip = true;
                else errorType = ParserErrorType::IsNot;
            } else if ((tok & ParserToken::LivModifier) && !bitsetIntersect(objects, session.loaded.isLiving)) {
                if (!exploreErrors) skip = true;
                else errorType = ParserErrorType::NotLiving;
            } else if (!(tok & ParserToken::VisOnlyModifier) &&
                       !bitsetIntersect(objects, session.loaded.isAccessible)) {
                if (!exploreErrors) skip = true;
                else errorType = ParserErrorType::NotAccessible;
            }

            if (!skip) {
                if (errorType != ParserErrorType::None) {
                    session.currentError = ParserErrorInfo{};
                    session.currentError.errorType = errorType;
                    if (!isBadMultiple) {
                        session.currentError.nounName = str;
                        session.currentError.nounIsPlural = (bitsetSingle(hnode.pluralObjs) == -1);
                    }
                    addMatch(session, localState, ParserToken::Error, start, state.wordIndex - 1);
                } else {
                    SentenceMatch& m = addMatch(session, localState, tok, start, state.wordIndex - 1);
                    m.obs = objects;
                    m.ordinal = ordinal;
                    localState.numObjs++;
                }
                parseRule(vm, session, localState);
            }
            objects = saveObs;
        }

        if (hnode.isAdj) {
            bitsetIntersect(objects, hnode.adjObjs);
        } else {
            return; // real "DEBUG_DEC; return;" -- not an adjective either, nothing left to chain onto
        }
    }
}

// real parse_rule() (packages/parser.c). STR_TOKEN/WRD_TOKEN/literal
// words and the rule terminator are restricted to what this slice's own
// tests exercise (real code all along); the OBJ/LIV/OBS/LVS case (real
// parse_obj(), below) and its own literal-mismatch recovery are now
// real and reachable for any rule with at most one object-family token
// (VerbRuleNode::objectTokenCount's own comment).
void parseRule(VM& vm, SentenceSession& session, MatchState& state) {
    const VerbRuleNode& node = *session.currentNode;
    const int numWords = static_cast<int>(session.words.size());

    while (true) {
        int tok = tokenAt(node, state.tokIndex++);
        if (state.wordIndex == numWords && tok) {
            return; // real "Ran out of words to parse."
        }
        int masked = tok & ~ParserToken::ChooseModifier;

        if (masked == 0) {
            if (state.wordIndex == numWords) weAreFinished(vm, session, state);
            return;
        }

        if (masked >= ParserToken::ObjA) {
            // real "case OBJ_TOKEN: ...: local_state = *state;
            // parse_obj(tok, &local_state, 0); if (!best_match &&
            // !best_error_match) { ...forward-search 'there is no X'
            // fallback...} return;"
            MatchState localState = state;
            parseObj(vm, session, localState, tok, 0);
            if (session.bestMatchWeight == 0 && session.bestErrorWeight == 0) {
                int start = state.wordIndex++;
                while (state.wordIndex <= numWords) {
                    MatchState local2 = state;
                    addMatch(session, local2, ParserToken::Error, start, state.wordIndex - 1);
                    session.currentError = ParserErrorInfo{};
                    session.currentError.errorType = ParserErrorType::ThereIsNo;
                    session.currentError.thereIsNoText = extractWordRange(session, start, state.wordIndex - 1);
                    parseRule(vm, session, local2);
                    state.wordIndex++;
                }
            }
            return;
        }

        if (masked == ParserToken::Str) {
            if (tokenAt(node, state.tokIndex) == 0) {
                // real "At end; match must be the whole thing."
                int start = state.wordIndex;
                state.wordIndex = numWords;
                addMatch(session, state, ParserToken::Str, start, state.wordIndex - 1);
                parseRule(vm, session, state);
            } else {
                int start = state.wordIndex++;
                while (state.wordIndex <= numWords) {
                    MatchState local = state;
                    addMatch(session, local, ParserToken::Str, start, state.wordIndex - 1);
                    parseRule(vm, session, local);
                    state.wordIndex++;
                }
            }
            return;
        }

        if (masked == ParserToken::Wrd) {
            addMatch(session, state, ParserToken::Wrd, state.wordIndex, state.wordIndex);
            state.wordIndex++;
            parseRule(vm, session, state);
            return;
        }

        // literal (tok <= 0)
        int litIndex = -(tok + 1);
        const auto& literals = ParserPackage::currentLiterals();
        bool matched = litIndex >= 0 && static_cast<size_t>(litIndex) < literals.size() &&
                       state.wordIndex < numWords && session.words[state.wordIndex].text == literals[litIndex];
        if (matched) {
            state.wordIndex++;
            continue;
        }
        // Mismatch: real code's own recovery (a forward search that
        // marks the *previous* match as an error, spanning through
        // wherever the literal is eventually found) only fires when the
        // immediately preceding token was itself an object-family one
        // -- reachable now for a rule shaped "OBJ <literal>" (e.g. "give
        // OBJ away"). A preceding STR token, a preceding WRD/literal
        // token, or this being the rule's very first token (real
        // "state->tok_index == 1") all just return without recording an
        // error either way, matching real code exactly.
        if (state.tokIndex == 1) return;
        // real code's own recovery switch reads the previous token's RAW
        // value here, unmasked -- unlike the top-of-loop switch above,
        // it does NOT strip CHOOSE_MODIFIER first, so a rule using
        // "OBJ:c" immediately before a literal genuinely falls through
        // to the real "default: return;" case instead of recovering (a
        // real, confirmed-faithful quirk, not a porting gap).
        int prevTokRaw = tokenAt(node, state.tokIndex - 2);
        bool prevWasObjectFamily =
            prevTokRaw == ParserToken::Obj || prevTokRaw == ParserToken::ObjA || prevTokRaw == ParserToken::Liv ||
            prevTokRaw == ParserToken::LivA || prevTokRaw == ParserToken::Obs ||
            prevTokRaw == (ParserToken::Obs | ParserToken::VisOnlyModifier) || prevTokRaw == ParserToken::Lvs ||
            prevTokRaw == (ParserToken::Lvs | ParserToken::VisOnlyModifier);
        if (!prevWasObjectFamily) return;

        bool found = false;
        while (state.wordIndex < numWords) {
            bool eq = (litIndex >= 0 && static_cast<size_t>(litIndex) < literals.size() &&
                       session.words[state.wordIndex].text == literals[litIndex]);
            state.wordIndex++;
            if (eq) {
                found = true;
                break;
            }
        }
        if (!found) return;

        SentenceMatch& last = session.matches[state.numMatches - 1];
        last.token = ParserToken::Error;
        last.last = state.wordIndex - 1;
        if (state.numErrors++ == 0) {
            session.currentError = ParserErrorInfo{};
            session.currentError.errorType = ParserErrorType::ThereIsNo;
            session.currentError.thereIsNoText = extractWordRange(session, last.first, state.wordIndex - 1);
        }
        // real "break;" out of the recovery switch -- falls to the top
        // of the outer while(1) loop, continuing at the token right
        // after the literal that was just recovered.
        continue;
    }
}

// real make_error_message() (packages/parser.c): the generic "You
// can't <verb> <words...>." message built when a can_/direct_/
// indirect_/do_ callback rejects a match with no explicit string of its
// own (a falsy int or no explicit `return`, ParserErrorType::Allocated's
// own comment). `which` names the 1-based position of the match that
// actually failed (0 for the generic can_-check failure, matching real
// code's own literal `0` at that one call site) -- an object-family
// match AT that position renders as "that " (real code never tries to
// describe the very thing that just failed); an object-family match
// BEFORE it renders via query_the_short() (a two-object rule's own
// first, already-resolved object -- unreachable today since
// objectTokenCount is always <= 1, kept faithfully anyway since it
// falls out of the same real formula, not a separate case); every
// object-family match from the (`which`+1)-th real "object slot"
// onward, or one that is itself a plural match, also renders as "that ".
void makeErrorMessage(VM& vm, SentenceSession& session, int which, ParserErrorInfo& err) {
    std::string buf = "You can't ";
    buf += session.words[0].text;
    buf += ' ';

    int cnt = 0;
    int ocnt = 0;
    int match = 0;
    for (int tok : session.currentNode->tokens) {
        if (tok == ParserToken::Str || tok == ParserToken::Wrd) {
            if (tok == ParserToken::Str && cnt == which - 1) {
                buf += "that ";
                cnt++;
                continue;
            }
            buf += extractWordRange(session, session.matches[match].first, session.matches[match].last);
            buf += ' ';
            cnt++;
            match++;
        } else if (tok <= 0) {
            const auto& literals = ParserPackage::currentLiterals();
            int litIndex = -(tok + 1);
            if (litIndex >= 0 && static_cast<size_t>(litIndex) < literals.size()) buf += literals[litIndex];
            buf += ' ';
        } else {
            // object-family (real make_error_message()'s own default
            // branch for a tok > 0 that isn't STR/WRD). Real code's own
            // "cnt == which - 1 || ++ocnt >= which || (PLURAL_MODIFIER)"
            // short-circuits -- ocnt is only touched when the first
            // check didn't already decide "that ", faithfully preserved
            // here rather than flattened, since a future two-object rule
            // would observe the difference.
            bool useThat = (cnt == which - 1);
            if (!useThat) {
                ++ocnt;
                useThat = (ocnt >= which) || (session.matches[match].token & ParserToken::PluralModifier);
            }
            if (useThat) {
                buf += "that ";
            } else {
                int num = session.matches[match].number;
                auto ob = (num >= 0 && static_cast<size_t>(num) < session.loaded.objects.size())
                              ? session.loaded.objects[static_cast<size_t>(num)]
                              : nullptr;
                buf += queryTheShort(vm, ob);
                buf += ' ';
            }
            cnt++;
            match++;
        }
    }
    if (!buf.empty()) buf.pop_back(); // real "p--;" -- nuke the trailing space
    buf += ".\n";

    err.errorType = ParserErrorType::Allocated;
    err.allocatedMessage = buf;
}

// real process_answer() (packages/parser.c). `wasDefined` replaces real
// code's own "!sv" (real apply() returns a null svalue_t* specifically
// for an undefined function; this driver instead checks
// VM::functionExists() up front at the call site, and passes the
// result here explicitly, rather than trying to infer it from the
// returned Value -- see the monostate handling below for why that
// distinction has to be made before the call, not after).
// Returns: 1 accept, 0 undefined/wrong-type (try next), -2 generated
// error (an explicit falsy int), -1 generated error (an explicit
// string), -3 abort (already have an equally good candidate).
int processAnswer(VM& vm, SentenceSession& session, MatchState& state, bool wasDefined, const Value& result,
                   int which) {
    if (!wasDefined) return 0;
    if (auto* s = std::get_if<std::string>(&result.data)) {
        if (state.numErrors == session.bestNumErrors) return -3;
        if (state.numErrors++ == 0) {
            session.currentError.errorType = ParserErrorType::Allocated;
            session.currentError.allocatedMessage = *s;
        }
        return -1;
    }
    // real "if (sv->type == T_NUMBER) { if (sv->u.number) return 1;
    // ... }" -- this driver's own monostate ("void") is folded into the
    // same falsy-zero branch here: a genuine LPC function with no
    // explicit `return` statement implicitly returns int 0 in real
    // semantics, but this driver's own Return opcode represents that
    // exact case as monostate instead of a real int64_t 0 (VM.cpp's own
    // "if (localStack.empty()) return Value{};") -- a pre-existing,
    // driver-wide representation choice unrelated to this efun, not
    // something to work around by treating a no-return can_/direct_/
    // do_ function as if it were undefined (which `wasDefined` above
    // already correctly rules out for a genuinely undefined name).
    // Every other real "not a number, not a string" result (an object/
    // array/mapping/closure) still falls through to the final "return
    // 0" below, matching real code's own "if (sv->type != T_STRING)
    // return 0;" exactly.
    if (std::holds_alternative<std::monostate>(result.data)) {
        if (state.numErrors == session.bestNumErrors) return -3;
        if (state.numErrors++ == 0) makeErrorMessage(vm, session, which, session.currentError);
        return -2;
    }
    if (auto* n = std::get_if<int64_t>(&result.data)) {
        if (*n != 0) return 1;
        if (state.numErrors == session.bestNumErrors) return -3;
        if (state.numErrors++ == 0) makeErrorMessage(vm, session, which, session.currentError);
        return -2;
    }
    return 0;
}

// real parallel_process_answer() (packages/parser.c): the per-candidate
// counterpart to processAnswer() above, used by parallelCheckFunctions()
// while probing ONE specific candidate object's own direct_/indirect_/
// do_-twice applies. Genuinely different rules from processAnswer():
// a plain (non-'#') string return is treated as ACCEPTANCE (not
// rejection) -- real code's own way of letting a direct_/indirect_
// callback attach a message to a *successful* match; an error is only
// signaled by a falsy int/no-explicit-return, or a string prefixed with
// '#' (the '#' itself stripped from the stored message). Returns: 1
// accepted, -1 rejected (an error was recorded), 0 undefined (try the
// next naming strategy). Does not touch state.numErrors at all, unlike
// processAnswer() -- real code's own "if (state->num_errors == 0)"
// gate below only asks whether the OUTER match already carries its own
// error (e.g. from parse_obj() itself), never increments it here.
int parallelProcessAnswer(VM& vm, SentenceSession& session, MatchState& state, bool wasDefined, const Value& result,
                           int which) {
    if (!wasDefined) return 0;
    if (auto* n = std::get_if<int64_t>(&result.data)) {
        if (*n != 0) return 1;
        if (state.numErrors == 0) makeErrorMessage(vm, session, which, session.parallelError);
        return -1;
    }
    // real code's own T_NUMBER branch never sees a "no explicit return"
    // case (svalue_t has no such state); this driver's own monostate
    // does, folded into the same falsy path as processAnswer() does,
    // for the same driver-wide representation reason (that function's
    // own comment).
    if (std::holds_alternative<std::monostate>(result.data)) {
        if (state.numErrors == 0) makeErrorMessage(vm, session, which, session.parallelError);
        return -1;
    }
    if (auto* s = std::get_if<std::string>(&result.data)) {
        session.parallelError = ParserErrorInfo{};
        if (!s->empty() && (*s)[0] == '#') {
            session.parallelError.errorType = ParserErrorType::Allocated;
            session.parallelError.allocatedMessage = s->substr(1);
            return -1;
        }
        session.parallelError.errorType = ParserErrorType::Allocated;
        session.parallelError.allocatedMessage = *s;
        return 1;
    }
    return 0;
}

// real use_last_parallel_error() (packages/parser.c): promotes
// session.parallelError into session.currentError, the first time any
// candidate for this match produced one.
bool useLastParallelError(SentenceSession& session, MatchState& state) {
    if (session.parallelError.errorType == ParserErrorType::None) return false;
    if (state.numErrors++ == 0) {
        session.currentError = session.parallelError;
        session.parallelError = ParserErrorInfo{};
    }
    return true;
}

// real save_last_parallel_error() (packages/parser.c): remembers a
// rejected plural-match candidate's own error for later (a do_-call's
// own push_bitvec_as_array(), errors_too=1) -- prepended, matching real
// code's own singly-linked-list insertion order exactly (see
// SavedError's own comment).
bool saveLastParallelError(SentenceSession& session, int obj) {
    if (session.parallelError.errorType == ParserErrorType::None) return false;
    session.parallelErrors.insert(session.parallelErrors.begin(), SavedError{session.parallelError, obj});
    session.parallelError = ParserErrorInfo{};
    return true;
}

// real cache_last_parallel_error() (packages/parser.c): moves
// session.parallelError into a caller-supplied scratch slot instead of
// useLastParallelError()'s own fixed session.currentError target or
// saveLastParallelError()'s own persistent session.parallelErrors list --
// used by dependentCheckFunctions()/checkOneRelation() below to hold a
// per-candidate error temporarily without yet committing it as the
// match's own final error, since a later candidate in the same scan
// might still succeed.
bool cacheLastParallelError(SentenceSession& session, ParserErrorInfo& storage) {
    if (session.parallelError.errorType == ParserErrorType::None) return false;
    storage = session.parallelError;
    session.parallelError = ParserErrorInfo{};
    return true;
}

// real use_cached_parallel_error() (packages/parser.c): the inverse of
// cacheLastParallelError() above -- commits a previously-cached error
// into session.currentError, the same "first error for this branch
// wins" gate useLastParallelError() itself already uses.
bool useCachedParallelError(SentenceSession& session, MatchState& state, ParserErrorInfo& err) {
    if (err.errorType == ParserErrorType::None) return false;
    if (state.numErrors++ == 0) {
        session.currentError = err;
        err = ParserErrorInfo{};
    }
    return true;
}

// real prefixes[] (packages/parser.c).
const char* const kPrefixes[] = {"can_", "direct_", "indirect_", "do_", "direct_", "indirect_"};

struct BuiltFunction {
    std::string functionName;
    std::vector<Value> args;
};

// real push_bitvec_as_array()'s own object-fill loop, factored out so
// errorInfoToValue()'s own ERR_AMBIG case (below) and pushBitvecAsArray()
// itself (further below, which also needs the error prefix) share it:
// real code's own loop assigns objects into the array from the END
// backward as it scans candidates in ascending index order, which
// observably leaves the result in DESCENDING index order -- confirmed
// directly from source, not a porting slip, and NOT conditional on
// whether an error prefix is involved (get_the_error()'s own ERR_AMBIG
// call passes errors_too=0 and still exhibits it).
std::vector<Value> objectsDescendingIndexOrder(const SentenceSession& session, const ObjBitset& bv) {
    std::vector<Value> objs;
    for (size_t i = 0; i < bv.size(); i++) {
        if (!bv[i]) continue;
        auto ob = session.loaded.objects[i];
        objs.push_back((!ob || ob->isDestructed()) ? Value(static_cast<int64_t>(0)) : Value(ob));
    }
    std::reverse(objs.begin(), objs.end());
    return objs;
}

// real get_the_error() (packages/parser.c), generalized to any
// (err, objIndex) pair -- real code's own single hardcoded `obj == -1`
// call site (ParserPackage.hpp's own comment on getTheError() below)
// plus push_bitvec_as_array()'s own per-rejected-candidate calls with a
// real object index, both real call sites of the same real function.
// `objIndex` of -1 (or an already-destructed/out-of-range object) is
// real push_undefined() -- confirmed real "const0u", i.e. a plain int 0
// (see this file's own getTheError() comment). error_type None (real
// code's own `default:` case, "no error at all") returns the real
// "-found_level" "how close did we get" signal instead of consulting
// master at all.
Value errorInfoToValue(VM& vm, SentenceSession& session, const ParserErrorInfo& err, int objIndex) {
    if (err.errorType == ParserErrorType::None) {
        return Value(static_cast<int64_t>(-session.foundLevel));
    }
    if (!vm.masterObject()) return Value(static_cast<int64_t>(0));

    bool objOk = objIndex >= 0 && static_cast<size_t>(objIndex) < session.loaded.objects.size() &&
                 session.loaded.objects[static_cast<size_t>(objIndex)] &&
                 !session.loaded.objects[static_cast<size_t>(objIndex)]->isDestructed();
    Value objArg = objOk ? Value(session.loaded.objects[static_cast<size_t>(objIndex)])
                          : Value(static_cast<int64_t>(0));

    std::vector<Value> args = {Value(static_cast<int64_t>(err.errorType)), objArg};
    switch (err.errorType) {
        case ParserErrorType::IsNot:
        case ParserErrorType::NotLiving:
        case ParserErrorType::NotAccessible:
            args.push_back(Value(err.nounName));
            args.push_back(Value(static_cast<int64_t>(err.nounIsPlural ? 1 : 0)));
            break;
        case ParserErrorType::Ambig: {
            auto arr = std::make_shared<Array>();
            arr->items = objectsDescendingIndexOrder(session, err.ambigObs);
            args.push_back(Value(arr));
            break;
        }
        case ParserErrorType::Ordinal:
            args.push_back(Value(static_cast<int64_t>(err.ordError)));
            break;
        case ParserErrorType::ThereIsNo:
            args.push_back(Value(err.thereIsNoText));
            break;
        case ParserErrorType::Allocated:
            args.push_back(Value(err.allocatedMessage));
            break;
        case ParserErrorType::BadMultiple:
        default:
            // BadMultiple: real code's own 2-arg call, nothing more to
            // push. ManyPaths: unreachable (two-object rules only, see
            // this file's own header comment on that family) -- never
            // actually produced by anything in this slice.
            break;
    }
    Value ret = vm.applyMaster("parser_error_message", std::move(args));
    if (std::holds_alternative<std::monostate>(ret.data)) return Value(static_cast<int64_t>(0));
    return ret;
}

// real push_bitvec_as_array() (packages/parser.c): the accepted
// candidates surviving a plural (OBS/LVS) match's own
// pluralCheckFunctions() pass, as an array -- optionally prefixed (real
// errors_too, true only for a do_-call's own final argument build) with
// one already-resolved error Value per rejected candidate, in the exact
// same head-first order saveLastParallelError() recorded them (real
// code's own singly-linked-list traversal order). Real code's own fill
// loop assigns objects into the array from the END backward as it scans
// candidates in ascending index order, which observably leaves the
// object portion in DESCENDING index order after the error prefix --
// confirmed directly from source, not a porting slip -- reproduced here
// by collecting ascending and reversing.
Value pushBitvecAsArray(VM& vm, SentenceSession& session, const ObjBitset& bv, bool errorsToo) {
    auto arr = std::make_shared<Array>();
    if (errorsToo) {
        for (const SavedError& se : session.parallelErrors) {
            arr->items.push_back(errorInfoToValue(vm, session, se.err, se.obj));
        }
    }
    std::vector<Value> objs = objectsDescendingIndexOrder(session, bv);
    arr->items.insert(arr->items.end(), objs.begin(), objs.end());
    return Value(arr);
}

// real make_function() (packages/parser.c). The OBJ-family branch
// (`omatch`/`which` position-matching bookkeeping) is now general --
// **updated 2026-08-19 (a later session)**: originally reduced to the
// single-object-per-rule case (omatch implicitly always 1, `which >= 4`
// unreachable), now generalized to a genuine two-object rule too
// (VerbRuleNode::objectTokenCount's own comment on when that is
// reachable). Provably equivalent to the old single-object-only version
// for every `which` value that version ever saw (0, 1, 3) -- confirmed
// by hand for each: `omatch` after this position's own increment is 1
// for a rule's only (or first) object token, so `omatch == which`
// selects `target` exactly when which == 1, `omatch > which` selects the
// real "push 0" fallback exactly when which == 0, and which == 3 always
// falls through to the already-resolved-value chain unchanged -- the
// naming formula below reduces identically too (see its own comment).
// `which >= 4` (checkOneRelation()'s own relational probe, real
// `direct_object`/`indirect_object` read instead of a candidate bitvec)
// and `omatch == 2` (a genuine second object-family token) are the two
// new cases a two-object rule actually reaches. `target` is the specific
// candidate object parallelCheckFunctions() is currently probing
// (which == 1 or 2) -- unused for which == 0/3 exactly as before.
BuiltFunction makeFunction(VM& vm, SentenceSession& session, int which, int tryIdx,
                            const std::shared_ptr<LpcObject>& target = nullptr) {
    BuiltFunction result;
    std::string name = kPrefixes[which];

    if (tryIdx < 2) {
        name += session.matchedVerb->matchName;
    } else {
        name += "verb";
        result.args.push_back(Value(session.matchedVerb->matchName));
    }

    // real "if (try == 3) { buf = strput(buf, end, \"_rule\"); buf++;
    // ... }" -- the name is truncated to exactly "<prefix>verb_rule"
    // for tryIdx == 3; real code's own token loop keeps running after
    // this (still pushing arguments), it just writes past the
    // truncating null, which no caller ever reads back. This port
    // reproduces the observable effect directly: stop appending to
    // `name` from here on, but keep processing every token's own
    // argument-pushing side effect exactly as real code does.
    bool nameTruncated = (tryIdx == 3);
    if (tryIdx == 3) {
        name += "_rule";
        result.args.push_back(Value(ParserPackage::ruleString(session.currentNode->tokens, ParserPackage::currentLiterals())));
    }

    int match = 0;
    int omatch = 0; // real omatch, pre-increment-checked (see the naming/value logic below)
    const auto& literals = ParserPackage::currentLiterals();
    for (int tok : session.currentNode->tokens) {
        if (!nameTruncated) name += '_';

        int masked = tok & ~ParserToken::ChooseModifier;
        if (masked >= ParserToken::ObjA) {
            SentenceMatch& m = session.matches[match];
            bool isLiving = (masked & ParserToken::LivModifier) != 0;
            // real "if (omatch+1 >= which || !(matches[match].token &
            // PLURAL_MODIFIER) || which >= 4) buf += obj/liv; else buf
            // += obs/lvs;" -- ported as the direct negation instead:
            // plural spelling only when omatch+1 < which AND this
            // match is genuinely plural AND which < 4. `omatch` here is
            // its PRE-increment value (the OBS/LVS case's own check runs
            // before the shared `omatch++` below, matching real code's
            // own "put_obj_value:" label ordering exactly) -- 0 for a
            // rule's first object token, 1 for its second. For a
            // single-object rule (omatch always 0 here) this reduces
            // to "(1 < which) && plural && (which < 4)", identical to
            // the old hardcoded "(which == 3) && plural" for every
            // which in {0, 1, 3} -- confirmed by hand, not just by
            // construction: which=0/1 always give 1<which==false either
            // way; which=3 gives 1<3==true and which<4==true, leaving
            // exactly "plural" in both versions.
            bool usePluralSpelling =
                (omatch + 1 < which) && (m.token & ParserToken::PluralModifier) && (which < 4);
            if (!nameTruncated) {
                if (isLiving) {
                    name += usePluralSpelling ? "lvs" : "liv";
                } else {
                    name += usePluralSpelling ? "obs" : "obj";
                }
            }

            omatch++;
            if (which >= 4) {
                // real "if (omatch == 1) push_object(loaded_objects[
                // direct_object >= 0 ? direct_object : 0]); else
                // push_object(loaded_objects[indirect_object >= 0 ?
                // indirect_object : 0]);" -- checkOneRelation()'s own
                // relational probe, the only place either global is
                // read. Real code indexes loaded_objects[] unconditionally
                // (no upper-bound check); this port keeps the same real
                // "-1 falls back to index 0" defensive floor but adds an
                // upper-bound guard too, since an out-of-range index
                // would be memory-unsafe here in a way real C's own
                // array-of-pointers is not -- not a behavior difference
                // in any reachable case, since checkObjectRelations()
                // below only ever sets these to a valid loaded-object
                // index before this branch can run.
                int idx = (omatch == 1) ? session.directObject : session.indirectObject;
                if (idx < 0) idx = 0;
                if (idx >= 0 && static_cast<size_t>(idx) < session.loaded.objects.size()) {
                    auto ob = session.loaded.objects[static_cast<size_t>(idx)];
                    result.args.push_back((!ob || ob->isDestructed()) ? Value(static_cast<int64_t>(0)) : Value(ob));
                } else {
                    result.args.push_back(Value(static_cast<int64_t>(0)));
                }
            } else if (omatch == which) {
                // parallelCheckFunctions()'s own per-candidate probe --
                // push the specific candidate currently being tested.
                result.args.push_back(target ? Value(target) : Value(static_cast<int64_t>(0)));
            } else if (omatch > which) {
                // real "push_number(0)" -- a later object-family slot
                // than the one currently being probed (which == 0's own
                // "no candidate chosen yet" case falls here too, since
                // omatch is always >= 1 by this point).
                result.args.push_back(Value(static_cast<int64_t>(0)));
            } else if (m.token == ParserToken::Error) {
                result.args.push_back(Value(static_cast<int64_t>(0)));
            } else if (m.token & ParserToken::PluralModifier) {
                result.args.push_back(pushBitvecAsArray(vm, session, m.obs, which == 3));
            } else if (m.number < 0) {
                result.args.push_back(Value(static_cast<int64_t>(0)));
            } else {
                auto ob = session.loaded.objects[static_cast<size_t>(m.number)];
                result.args.push_back((!ob || ob->isDestructed()) ? Value(static_cast<int64_t>(0)) : Value(ob));
            }
            match++;
            continue;
        }
        if (masked == ParserToken::Str) {
            if (!nameTruncated) name += "str";
            result.args.push_back(Value(extractWordRange(session, session.matches[match].first, session.matches[match].last)));
            match++;
            continue;
        }
        if (masked == ParserToken::Wrd) {
            if (!nameTruncated) name += "wrd";
            result.args.push_back(Value(extractWordRange(session, session.matches[match].first, session.matches[match].last)));
            match++;
            continue;
        }
        // literal
        int litIndex = -(tok + 1);
        std::string litText = (litIndex >= 0 && static_cast<size_t>(litIndex) < literals.size()) ? literals[litIndex] : "";
        if (tryIdx == 0) {
            if (!nameTruncated) name += litText;
        } else if (tryIdx < 3) {
            if (!nameTruncated) name += "word";
            result.args.push_back(Value(litText));
        }
        // tryIdx == 3: real code contributes neither a name segment
        // nor an argument for a literal token.
    }

    result.functionName = name;
    return result;
}

// real push_real_names() (packages/parser.c).
std::vector<Value> pushRealNames(SentenceSession& session, int tryIdx) {
    std::vector<Value> args;
    if (tryIdx >= 2) {
        args.push_back(Value(extractWordRange(session, 0, 0)));
    }
    int match = 0;
    for (int tok : session.currentNode->tokens) {
        if (tok > 0) {
            args.push_back(Value(extractWordRange(session, session.matches[match].first, session.matches[match].last)));
            match++;
        }
    }
    return args;
}

// real check_functions() (packages/parser.c): probes can_ (real
// which == 0) under all four real naming strategies, first against
// `player`, then (tryIdx 4-7, i.e. tryIdx % 4 again) against the rule's
// own registering object (session.currentNode->handler) if the first
// four attempts found nothing.
bool checkFunctions(VM& vm, SentenceSession& session, MatchState& state, const std::shared_ptr<LpcObject>& player) {
    std::shared_ptr<LpcObject> ob = player;
    if (!ob || ob->isDestructed()) return false;

    int ret = 0;
    for (int tryIdx = 0; !ret && tryIdx < 8; tryIdx++) {
        if (tryIdx == 4) {
            ob = session.currentNode->handler.lock();
            if (!ob || ob->isDestructed()) return false;
        }
        BuiltFunction built = makeFunction(vm, session, 0, tryIdx % 4);
        std::vector<Value> extra = pushRealNames(session, tryIdx % 4);
        built.args.insert(built.args.end(), extra.begin(), extra.end());

        bool exists = vm.functionExists(ob, built.functionName);
        Value result = exists ? vm.callFunction(ob, built.functionName, built.args) : Value{};
        ret = processAnswer(vm, session, state, exists, result, 0);
        if (ob->isDestructed()) return false;
        if (ret == -3) return false;
    }
    if (!ret) {
        if (state.numErrors == session.bestNumErrors) return false;
        if (state.numErrors++ == 0) makeErrorMessage(vm, session, 0, session.currentError);
    }
    return true;
}

// real parallel_check_functions() (packages/parser.c): probes
// direct_/indirect_ (real `which`, 1 or 2 -- always 1 in this slice,
// since a rule has at most one object-family token) under all four real
// naming strategies against ONE specific candidate object, first
// against `candidate` itself (real code's own confusingly-named `obj`
// parameter, always the candidate here -- see makeFunction()'s own
// comment for why `which` doubles as both the prefix-selector index
// AND the object-position index), then against the rule's own
// registering object if the first four attempts found nothing.
int parallelCheckFunctions(VM& vm, SentenceSession& session, MatchState& state,
                            const std::shared_ptr<LpcObject>& candidate, int which) {
    session.parallelError = ParserErrorInfo{};
    std::shared_ptr<LpcObject> ob = candidate;
    if (!ob || ob->isDestructed()) return 0;

    int ret = 0;
    for (int tryIdx = 0; !ret && tryIdx < 8; tryIdx++) {
        if (tryIdx == 4) {
            ob = session.currentNode->handler.lock();
            if (!ob || ob->isDestructed()) return 0;
        }
        BuiltFunction built = makeFunction(vm, session, which, tryIdx % 4, candidate);
        std::vector<Value> extra = pushRealNames(session, tryIdx % 4);
        built.args.insert(built.args.end(), extra.begin(), extra.end());

        bool exists = vm.functionExists(ob, built.functionName);
        Value result = exists ? vm.callFunction(ob, built.functionName, built.args) : Value{};
        ret = parallelProcessAnswer(vm, session, state, exists, result, which);
        if (ob->isDestructed()) return 0;
    }
    if (!ret) {
        if (state.numErrors == 0) makeErrorMessage(vm, session, 0, session.parallelError);
        return 0;
    }
    return ret == 1;
}

// real singular_check_functions() (packages/parser.c): resolves a
// non-plural object-family match's own candidate bitvec down to exactly
// one accepted object (m.number), or records the right error
// (Ambig/Ordinal/whatever the last rejecting candidate's own callback
// produced) when that isn't possible. Ordinal-scoped (m.ordinal != 0,
// "the second sword") and plain ("a sword") cases share the same
// candidate scan but diverge sharply in how they interpret the result,
// exactly matching real code's own two-branch tail.
void singularCheckFunctions(VM& vm, SentenceSession& session, MatchState& state, int which, SentenceMatch& m) {
    int ordinal = m.ordinal;
    int ord2 = m.ordinal;
    bool hasOrdinal = (m.ordinal != 0);
    bool wasError = false;
    int ambig = 0;
    int match = -1;

    for (size_t i = 0; i < m.obs.size(); i++) {
        if (!m.obs[i]) continue;
        int ret = parallelCheckFunctions(vm, session, state, session.loaded.objects[i], which);
        if (ret) {
            if (hasOrdinal) {
                ord2--;
                if (ordinal < 0 || --ordinal == 0) {
                    if (ordinal == -2) state.numErrors--;
                    if (useLastParallelError(session, state)) {
                        m.token = ParserToken::Error;
                        if (ordinal != -1) return;
                        ordinal = -2;
                    } else {
                        m.number = static_cast<int>(i);
                        return;
                    }
                }
            } else {
                if (ambig++ == 0) {
                    if (useLastParallelError(session, state)) {
                        wasError = true;
                        m.token = ParserToken::Error;
                    } else {
                        wasError = false;
                        match = static_cast<int>(i);
                    }
                    if (m.token & ParserToken::ChooseModifier) {
                        if (match >= 0) m.number = match;
                        return;
                    }
                } else {
                    match = -1;
                }
            }
        } else {
            if (hasOrdinal && (ordinal == -1 || --ord2 == 0)) {
                session.secondParallelError = session.parallelError;
                session.parallelError = ParserErrorInfo{};
            }
            m.obs[i] = false;
        }
    }

    if (!hasOrdinal) {
        if (ambig == 1) {
            m.number = match;
            return;
        }
        if (wasError) state.numErrors--;
        m.token = ParserToken::Error;
        if (ambig == 0 && useLastParallelError(session, state)) return;
        if (state.numErrors++ == 0) {
            session.currentError = ParserErrorInfo{};
            session.currentError.errorType = ParserErrorType::Ambig;
            session.currentError.ambigObs = m.obs;
        }
    } else {
        if (ordinal == -2) return;
        m.token = ParserToken::Error;
        if (state.numErrors++ == 0) {
            if (ord2 <= 0) {
                session.currentError = session.secondParallelError;
                session.secondParallelError = ParserErrorInfo{};
            } else {
                session.currentError = ParserErrorInfo{};
                session.currentError.errorType = ParserErrorType::Ordinal;
                session.currentError.ordError = static_cast<int>(bitsetCount(m.obs));
            }
        }
    }
}

// real plural_check_functions() (packages/parser.c): tests every
// candidate independently, keeping whichever accept (no attempt at
// resolving to one specific object -- a plural match's own SentenceMatch
// ::number is never set at all, only its obs bitvec is narrowed down to
// the survivors); a rejected candidate's own error is saved (not
// discarded) so a later do_-call array can report it (pushBitvecAsArray()
// ::errorsToo).
void pluralCheckFunctions(VM& vm, SentenceSession& session, MatchState& state, int which, SentenceMatch& m) {
    bool foundOne = false;
    for (size_t i = 0; i < m.obs.size(); i++) {
        if (!m.obs[i]) continue;
        int ret = parallelCheckFunctions(vm, session, state, session.loaded.objects[i], which);
        if (!ret || saveLastParallelError(session, static_cast<int>(i))) {
            m.obs[i] = false;
        } else {
            foundOne = true;
        }
    }
    if (!foundOne && useLastParallelError(session, state)) {
        m.token = ParserToken::Error;
    }
}

// real dependent_check_functions() (packages/parser.c) -- item 9's own
// two-object family, first real piece. The two-object analog of
// pluralCheckFunctions() above: narrows `m`'s own candidate bitvec down
// to objects that individually pass the generic per-object can_ check
// (parallelCheckFunctions(), the same `which` -- 1 for the direct slot,
// 2 for the indirect -- weAreFinished() below already threads through
// singularCheckFunctions()/pluralCheckFunctions() for the one-object
// case), remembering the FIRST surviving candidate's own index in
// m.number. That index is NOT yet the final answer -- it is only a
// placeholder real make_function()'s own "which == 1/2, omatch does not
// match which" fallback branch (makeFunction()'s own comment) reads
// while the OTHER object slot's candidate is being probed, before
// checkObjectRelations() below has actually decided the real winning
// pair. checkObjectRelations() is what performs the real cross-product
// pairing test and overwrites m.number with the true final answer.
void dependentCheckFunctions(VM& vm, SentenceSession& session, MatchState& state, int which, SentenceMatch& m) {
    bool foundOne = false;
    ParserErrorInfo errinfo;
    for (size_t i = 0; i < m.obs.size(); i++) {
        if (!m.obs[i]) continue;
        int ret = parallelCheckFunctions(vm, session, state, session.loaded.objects[i], which);
        if (!ret || cacheLastParallelError(session, errinfo)) {
            m.obs[i] = false;
        } else {
            if (!foundOne) m.number = static_cast<int>(i);
            foundOne = true;
        }
    }
    if (!foundOne && (useCachedParallelError(session, state, errinfo) || useLastParallelError(session, state))) {
        m.token = ParserToken::Error;
    }
}

namespace CheckRelation {
constexpr int DirectOk = 1;
constexpr int IndirectOk = 2;
constexpr int ErrorRelation = 4;
constexpr int RelationComplete = 8;
} // namespace CheckRelation

// real check_one_relation() (packages/parser.c): tests whether ONE
// specific (direct, indirect) candidate PAIR -- both already fixed in
// session.directObject/session.indirectObject by the caller,
// checkObjectRelations() below -- is jointly valid, via the real
// which=4/which=5 relational naming convention (makeFunction()'s own
// `which >= 4` branch, the only place either global is ever read
// instead of a match's own candidate bitvec: real can_give_obj_to_liv()-
// style names, exactly the naming convention dead-souls.net's own Dead
// Souls `lib/verbs/items/give.c` genuinely defines, confirmed against
// real source). `directFirst` selects which of the pair is probed with
// which=4 first (real code's own `direct_first` parameter -- driven by
// `indirect_unique` at checkObjectRelations()'s own one real call site
// below, not simply "always probe direct first").
//
// **Real, confirmed-inert quirk ported faithfully, not silently fixed:**
// the second `parallel_check_functions()` call below plainly
// *reassigns* `res` in real code ("res = parallel_check_functions(...)"),
// discarding whatever CHECK_DIRECT_OK/CHECK_INDIRECT_OK bit the FIRST
// call already OR'd in, if that second call itself returns 0 -- the
// early "if (!res) return res | CHECK_ERROR_RELATION;" then reports
// neither OK bit even though the first probe genuinely succeeded.
// Confirmed harmless: checkObjectRelations() below, the only real
// caller, never actually reads CHECK_DIRECT_OK/CHECK_INDIRECT_OK at
// all, only CHECK_RELATION_COMPLETE -- so this loses nothing observable
// through this one real call path, but is reproduced exactly (plain
// reassignment, not `|=`) rather than "corrected," per this row's own
// standing fidelity-first practice.
int checkOneRelation(VM& vm, SentenceSession& session, MatchState& state, bool directFirst,
                      ParserErrorInfo& errinfo) {
    ParserErrorInfo err;
    size_t firstIdx = static_cast<size_t>(directFirst ? session.directObject : session.indirectObject);
    int res = parallelCheckFunctions(vm, session, state, session.loaded.objects[firstIdx], directFirst ? 4 : 5) ? 1 : 0;
    if (!res) {
        return CheckRelation::ErrorRelation;
    }
    res |= directFirst ? CheckRelation::DirectOk : CheckRelation::IndirectOk;
    if (cacheLastParallelError(session, err)) {
        res |= CheckRelation::ErrorRelation;
    }
    size_t secondIdx = static_cast<size_t>(directFirst ? session.indirectObject : session.directObject);
    res = parallelCheckFunctions(vm, session, state, session.loaded.objects[secondIdx], directFirst ? 5 : 4) ? 1 : 0;
    if (!res) {
        if (err.errorType != ParserErrorType::None) errinfo = err;
        return res | CheckRelation::ErrorRelation;
    }
    res |= directFirst ? CheckRelation::IndirectOk : CheckRelation::DirectOk;
    if (err.errorType == ParserErrorType::None && cacheLastParallelError(session, err)) {
        res |= CheckRelation::ErrorRelation;
    }
    res |= CheckRelation::RelationComplete;
    if (err.errorType != ParserErrorType::None) errinfo = err;
    return res;
}

// real check_object_relations() (packages/parser.c:2312-2493) -- item 9's
// own two-object family, second and final piece for this slice. Real
// source read in full (not just the portion an earlier session's own
// scoping pass had stopped at) before writing this port; two genuine,
// confirmed real bugs found are ported faithfully and flagged inline
// below rather than silently fixed or silently reproduced without a
// note, per this session's own explicit instruction.
//
// **Update, 2026-08-19 (a further session): real, handles a plural
// object-family slot on either side too, not just the both-singular
// case a still-earlier session deliberately shipped first.** A fresh
// re-read of the whole function, specifically hunting for a separate
// plural-only code path, found none: `direct_unique`/`indirect_unique`
// (below, straight from each match's own real PLURAL_MODIFIER bit) are
// the ONLY plurality-dependent decisions in this entire function --
// whether an ambiguity check applies at all (a genuinely plural side is
// allowed to accumulate more than one accepted candidate without
// erroring, matching real "give all the coins to the guard"), and
// whether the final resolved value is a single object index or the
// whole accumulated set. `dependentCheckFunctions()`/`checkOneRelation()`
// have no plurality-dependent logic anywhere in real source either.
// `parseRulesFor()` below no longer gates a plural-involving two-object
// node out at all.
void checkObjectRelations(VM& vm, SentenceSession& session, MatchState& state) {
    int direct = -1, indirect = -1;
    for (int i = 0; i < state.numMatches; i++) {
        if (session.matches[i].token & ParserToken::ObjA) {
            if (direct < 0) direct = i; else indirect = i;
        } else if (session.matches[i].token == ParserToken::Error) {
            return;
        }
    }

    ObjBitset& dirObjs = session.matches[direct].obs;
    ObjBitset& indirObjs = session.matches[indirect].obs;

    if (session.matches[indirect].ordinal) {
        // real "if the indirect object is used with ordinal number,
        // choose only that single indirect object" -- "give the sword
        // to the second guard" must not silently consider every guard.
        //
        // **Real, confirmed bug ported faithfully, flagged, not
        // silently fixed:** real code's own scan starts at whatever `i`
        // was left at by the OBJ_A_TOKEN-scan loop just above (real
        // "if (ord > 0) i = 0;" -- `i` is reset to 0 only for a
        // POSITIVE ordinal; for a negative one -- real code's own "-1
        // means the last candidate" convention, matching
        // singularCheckFunctions()'s own identical convention elsewhere
        // in this file -- `i` is left at whatever the earlier loop's own
        // exit value was, `state->num_matches` (always 2 here, since
        // this function is only ever reached with exactly two
        // object-family matches)). Real `i` indexes bitvec_t *words*,
        // not individual objects (real "BPI * i + k" -- BPI ==
        // sizeof(int)*8 == 32 bits per word, packages/parser.h:46,
        // confirmed directly), so the real absolute starting OBJECT
        // index this bug produces is `BPI * state->num_matches` == 64,
        // not object index 0 -- silently skipping every candidate below
        // index 64 for a negative-ordinal indirect object. Confirmed
        // directly against source, not a porting slip: a real, if
        // narrow, bug in real FluffOS 2.9's own
        // check_object_relations() -- "give the sword to the last
        // guard" would wrongly miss every guard loaded before index 64
        // in real FluffOS too. This driver's own ObjBitset is already a
        // flat one-bool-per-object vector (HashEntry's own comment: the
        // real bitvec_t word/bit split is a pure sizing/perf detail with
        // no LPC-visible contract on its own), so the real "BPI * i + k"
        // formula's own absolute-index RESULT is what is ported here
        // directly -- the same starting object index real code's own
        // bug actually produces, not a re-derivation of the word-chunk
        // mechanics that produced it.
        constexpr int kBpi = 32; // real BPI, packages/parser.h:46
        int ord = session.matches[indirect].ordinal;
        int i = (ord > 0) ? 0 : (state.numMatches * kBpi);
        ObjBitset& bv = indirObjs;
        bool found = false;
        for (; static_cast<size_t>(i) < bv.size(); i++) {
            if (!bv[static_cast<size_t>(i)]) continue;
            if (--ord == 0) {
                int chosen = i;
                bv.assign(bv.size(), false);
                bv[static_cast<size_t>(chosen)] = true;
                found = true;
                break;
            }
        }
        if (!found) {
            session.matches[indirect].token = ParserToken::Error;
            if (state.numErrors++ == 0) {
                session.currentError = ParserErrorInfo{};
                session.currentError.errorType = ParserErrorType::Ordinal;
                session.currentError.ordError = static_cast<int>(bitsetCount(bv)) + 1;
            }
            return;
        }
    }

    bool directUnique = !(session.matches[direct].token & ParserToken::PluralModifier);
    bool indirectUnique = !(session.matches[indirect].token & ParserToken::PluralModifier);
    int directOrdinal = session.matches[direct].ordinal;
    if (!directOrdinal) directOrdinal = -1;

    // real `directs`/`indirects` (bitvec_t, zero-initialized once at the
    // very top of the real function, declared here instead since this
    // port's own ERR_MANY_PATHS check below and the main pairing loop
    // are the only two real readers/writers and both need the exact
    // same variables, not two independently-zeroed copies).
    ObjBitset directsAccum(session.loaded.objects.size(), false);
    ObjBitset indirectsAccum(session.loaded.objects.size(), false);

    if (!session.matches[indirect].ordinal) {
        // real "check if there's not SO much possibilities" --
        // ERR_MANY_PATHS guard against a combinatorial-explosion N*M
        // scan. **Real, confirmed bug ported faithfully, flagged, not
        // silently fixed:** real code counts `directs`/`indirects` here
        // (the two ACCUMULATOR bitvecs the main scan below fills in,
        // still empty at this point -- always 0*0==0, never >= 80), not
        // `dir_objs`/`indir_objs` (the two CANDIDATE bitvecs that
        // actually hold this rule's real candidate counts) -- so this
        // guard can never fire at all in real FluffOS 2.9, regardless of
        // how many real candidates either side has. Ported exactly: the
        // same always-empty accumulators are counted here, not the real
        // candidate sets, so this guard stays real-faithfully inert.
        int directCount = static_cast<int>(bitsetCount(directsAccum));
        int indirectCount = static_cast<int>(bitsetCount(indirectsAccum));
        if (directCount * indirectCount >= 80) {
            state.numErrors++;
            session.currentError = ParserErrorInfo{};
            session.currentError.errorType = ParserErrorType::ManyPaths;
            return;
        }
    }

    // real local `parser_error_t err` (declared at the top of the real
    // function, initialized to error_type 0/None there) -- distinct from
    // session.currentError/session.parallelError: a scratch slot passed
    // by reference into every checkOneRelation() call across the whole
    // double loop below, only ever committed into session.currentError
    // at this function's own tail, via useCachedParallelError().
    ParserErrorInfo err;
    int foundDirect = -1, foundIndirect = -1;
    bool finished = false;

    for (size_t i = 0; !finished && i < dirObjs.size(); i++) {
        if (!dirObjs[i]) continue;
        session.directObject = static_cast<int>(i);
        for (size_t l = 0; l < indirObjs.size(); l++) {
            if (!indirObjs[l]) continue;
            session.indirectObject = static_cast<int>(l);

            int ret = checkOneRelation(vm, session, state, indirectUnique, err);
            if (!(ret & CheckRelation::RelationComplete)) continue;

            if (indirectUnique && foundIndirect >= 0 && foundIndirect != session.indirectObject) {
                session.matches[indirect].token = ParserToken::Error;
                if (state.numErrors++ == 0) {
                    session.currentError = ParserErrorInfo{};
                    session.currentError.errorType = ParserErrorType::Ambig;
                    session.currentError.ambigObs = session.matches[indirect].obs;
                }
                return;
            }
            if (directOrdinal > 0) directOrdinal--;
            if (directOrdinal <= 0 && directUnique && foundDirect >= 0 && foundDirect != session.directObject) {
                session.matches[indirect].token = ParserToken::Error;
                if (state.numErrors++ == 0) {
                    session.currentError = ParserErrorInfo{};
                    session.currentError.errorType = ParserErrorType::Ambig;
                    session.currentError.ambigObs = session.matches[direct].obs;
                }
                return;
            }
            if (!(ret & CheckRelation::ErrorRelation)) {
                if (directOrdinal <= 0) directsAccum[static_cast<size_t>(session.directObject)] = true;
                indirectsAccum[static_cast<size_t>(session.indirectObject)] = true;
            }
            if (directOrdinal <= 0) foundDirect = session.directObject;
            foundIndirect = session.indirectObject;
        }
        // **Real, confirmed bug ported faithfully, flagged, not silently
        // fixed:** real code's own early-exit test is
        // "if (found_direct && (!direct_ordinal || (direct_unique &&
        // CHOOSE_MODIFIER)))" -- `found_direct` tested for plain C
        // truthiness, not `found_direct >= 0` the way every OTHER use of
        // this exact variable in this same real function tests it
        // (including four lines above, and again at this function's own
        // tail). Since found_direct's real sentinel is -1 (itself
        // truthy in C) and a real object index of 0 is the one value
        // this test cannot distinguish from "not found yet", the
        // condition is almost always true whenever anything has been
        // found (regardless of index), and would be FALSE for the one
        // legitimate case (found_direct == 0) where a real early exit
        // was actually earned. Confirmed via the surrounding gate,
        // though, that this bug is inert for the ordinary case this
        // sub-slice targets: directOrdinal defaults to -1 (real
        // "!direct_ordinal" is then false, since -1 is truthy) and
        // CHOOSE_MODIFIER is rare, so the whole condition is FALSE for
        // an ordinary no-ordinal rule regardless of foundDirect's value
        // either way -- the loop simply runs to full completion instead
        // of exiting early, which changes nothing about the final
        // accumulated directsAccum/indirectsAccum result, only whether
        // every candidate pair gets visited. Ported exactly (the real
        // truthy test, not `>= 0`) rather than silently corrected.
        if (foundDirect && (directOrdinal == 0 ||
                             (directUnique && (session.matches[direct].token & ParserToken::ChooseModifier)))) {
            finished = true;
        }
    }

    dirObjs = directsAccum;
    indirObjs = indirectsAccum;
    session.matches[direct].number = directUnique ? foundDirect : 0;
    session.matches[indirect].number = indirectUnique ? foundIndirect : 0;

    if (foundDirect < 0) {
        if (useCachedParallelError(session, state, err) || useLastParallelError(session, state)) {
            session.matches[direct].token = ParserToken::Error;
        }
    } else if (foundIndirect < 0) {
        if (useCachedParallelError(session, state, err) || useLastParallelError(session, state)) {
            session.matches[indirect].token = ParserToken::Error;
        }
    }
}

// real we_are_finished() (packages/parser.c): once the generic can_
// check passes, resolves every object-family match in turn
// (singularCheckFunctions()/pluralCheckFunctions() for a one-object
// node; pluralCheckFunctions()/dependentCheckFunctions() (whichever
// applies per match) plus checkObjectRelations() below for a real
// two-object node, singular or plural on either side alike --
// checkObjectRelations()'s own header comment), then either records the
// best error seen so far or commits this node as the new best match and
// pre-builds all four real do_ naming variants.
void weAreFinished(VM& vm, SentenceSession& session, MatchState& state) {
    if (session.foundLevel < 2) session.foundLevel = 2;
    if (session.bestMatchWeight >= session.currentNode->weight) return;
    if (state.numErrors) {
        if (state.numErrors > session.bestNumErrors) return;
        if (state.numErrors == session.bestNumErrors && session.currentNode->weight < session.bestErrorWeight) return;
    }

    if (!checkFunctions(vm, session, state, session.caller)) return;

    session.parallelErrors.clear(); // real clear_parallel_errors(&parallel_errors)

    for (int which = 1, mtch = 0; which < 3 && mtch < state.numMatches; mtch++) {
        int tok = session.matches[mtch].token;
        if (tok == ParserToken::Error) {
            which++;
            continue;
        }
        if (!(tok & ParserToken::ObjA)) continue;

        SentenceMatch& m = session.matches[mtch];
        if (tok & ParserToken::PluralModifier) {
            pluralCheckFunctions(vm, session, state, which, m);
        } else if (state.numObjs == 2) {
            dependentCheckFunctions(vm, session, state, which, m);
        } else {
            singularCheckFunctions(vm, session, state, which, m);
        }
        which++;
    }
    // real "if (state->num_objs == 2 && !state->num_errors)
    // check_object_relations(state);" -- only attempted once BOTH
    // matches independently survived dependentCheckFunctions() above
    // (a state.numErrors from either side's own rejection means there is
    // no valid pair to even look for).
    if (state.numObjs == 2 && !state.numErrors) {
        checkObjectRelations(vm, session, state);
    }

    if (state.numErrors) {
        int weight = session.currentNode->weight;
        if (session.currentError.errorType == ParserErrorType::ThereIsNo) {
            // real "ERR_THERE_IS_NO is basically a STR in place of an
            // OBJ, so is weighted far too highly. Give it approximately
            // the same weight as a STR."
            weight = 1;
        }
        if (state.numErrors == session.bestNumErrors && weight <= session.bestErrorWeight) return;
        session.bestError = session.currentError;
        session.currentError = ParserErrorInfo{};
        session.bestNumErrors = state.numErrors;
        session.bestErrorWeight = weight;
        return;
    }

    session.bestMatchWeight = session.currentNode->weight;
    auto handler = session.currentNode->handler.lock();
    if (!handler || handler->isDestructed()) return;

    SentenceMatchResult result;
    result.handler = handler;
    for (int tryIdx = 0; tryIdx < 4; tryIdx++) {
        BuiltFunction built = makeFunction(vm, session, 3, tryIdx);
        std::vector<Value> extra = pushRealNames(session, tryIdx);
        built.args.insert(built.args.end(), extra.begin(), extra.end());
        result.res[tryIdx].functionName = built.functionName;
        result.res[tryIdx].args = std::move(built.args);
    }
    session.bestResult = std::move(result);
}

// real parse_rules() (packages/parser.c): tries every rule node
// registered under the matched verb, skipping any node whose weight
// cannot beat the current best match, and any node whose own
// lit[0]/lit[1] prefilter fails. **Update, 2026-08-19 (a further
// session): a two-object node is no longer skipped regardless of
// plurality on either side** (VerbRuleNode::objectTokenCount's own
// comment) -- both-singular and plural-involving two-object rules
// (e.g. "give OBS to LIV") are both real as of this session.
// `restrictedHandler` is real parse_restricted (set only by
// parse_my_rules(), always null for parse_sentence() itself) -- real
// "(!parse_restricted || parse_vn->handler == parse_restricted)",
// confirmed directly: a null restriction matches every node exactly as
// before, a non-null one keeps only nodes registered by that exact
// object.
void parseRulesFor(VM& vm, SentenceSession& session, const std::shared_ptr<LpcObject>& restrictedHandler) {
    for (const VerbRuleNode& node : session.matchedVerb->nodes) {
        if (restrictedHandler && node.handler.lock() != restrictedHandler) continue;
        if (session.bestMatchWeight > node.weight) continue;

        int pos = 0;
        if (node.lit[0] != -1) {
            pos = checkLiteral(session, node.lit[0], 1);
            if (pos == 0) continue;
        }
        if (node.lit[1] != -1) {
            if (checkLiteral(session, node.lit[1], pos) == 0) continue;
        }

        session.currentNode = &node;
        MatchState state;
        state.tokIndex = 0;
        state.wordIndex = 1;
        state.numMatches = 0;
        state.numErrors = 0;
        parseRule(vm, session, state);
    }
}

// real do_the_call() (packages/parser.c): invokes the first of the four
// pre-built do_ variants that is actually defined on the handler.
void doTheCall(VM& vm, SentenceSession& session) {
    auto ob = session.bestResult->handler.lock();
    if (!ob || ob->isDestructed()) return;
    for (int i = 0; i < 4; i++) {
        if (ob->isDestructed()) return;
        const auto& call = session.bestResult->res[i];
        if (vm.functionExists(ob, call.functionName)) {
            vm.callFunction(ob, call.functionName, call.args);
            return;
        }
    }
    throw LpcRuntimeError("parse_sentence: parse accepted, but no do_* function found on /" + ob->filename());
}

// real get_the_error() (packages/parser.c), with `obj` fixed at real
// code's own -1 -- confirmed directly that every real call site
// (f_parse_sentence(), f_parse_my_rules()) passes -1 here (the OTHER
// real call site, push_bitvec_as_array()'s own per-rejected-candidate
// use with a real object index, is errorInfoToValue()'s own direct
// caller instead, above).
Value getTheError(VM& vm, SentenceSession& session) { return errorInfoToValue(vm, session, session.bestError, -1); }

// real global `pi` (parse_info_t *, packages/parser.c): non-null for the
// whole duration of any parse_sentence()/parse_my_rules() call currently
// on the C call stack, set right before matching begins and unconditionally
// cleared back to 0 when that call unwinds (real free_parse_globals(),
// installed as an error-handler stack entry so it fires whether the call
// finishes normally or via error()). Represented here as a plain bool
// rather than a real pointer, since nothing in this driver ever reads
// *through* it the way real code's own `pi = parse_user->pinfo` does --
// the only real consumer, f_parse_my_rules()'s own "if (pi) error(...)"
// guard, only ever tests it for null/non-null (ParseMyRules() below).
// Deliberately a single flag, not a save/restore depth counter: real
// code's own single global `pi` means a genuinely nested call (real
// parse_sentence()'s own recursion guard is commented out, see
// SentenceSession's own comment on this exact point) has its inner
// call's free_parse_globals() clobber `pi` back to 0 while an outer call
// is still logically in progress on the C stack -- a real, if reckless,
// consequence of real code's own single shared global this port
// reproduces faithfully via the same unconditional-clear-on-unwind
// shape, not a save-and-restore that would silently behave more safely
// than real FluffOS actually does.
bool& parseInProgressFlag() {
    static bool flag = false;
    return flag;
}
struct ParseInProgressGuard {
    ParseInProgressGuard() { parseInProgressFlag() = true; }
    ~ParseInProgressGuard() { parseInProgressFlag() = false; }
};

// real parse_sentence()'s own "find an interpretation, first word must
// be shared (verb)" loop (packages/parser.c), factored out so both
// ParserPackage::parseSentence() and ParserPackage::parseMyRules() share
// it verbatim -- real code's own f_parse_sentence() and f_parse_my_rules()
// both call the identical static parse_sentence() helper themselves, this
// is the same split. `restrictedHandler` is threaded straight through to
// parseRulesFor() (real parse_restricted -- see that function's own
// comment); null for parseSentence()'s own call, matching real code's own
// "parse_restricted = 0" default. `verbTable` is ParserPackage::verbs()
// itself, passed in rather than accessed directly since this is a free
// function outside the class (verbs() is private) -- the same
// pass-it-in-explicitly shape findPlainEntry() above already uses.
void runParseMatch(VM& vm, SentenceSession& session, const std::shared_ptr<LpcObject>& restrictedHandler,
                    std::unordered_map<std::string, std::vector<VerbEntry>>& verbTable) {
    std::vector<SentenceWord> rawWords = splitSentenceWords(session.rawInput);

    for (size_t i = 1; i <= rawWords.size(); i++) {
        std::string candidate = rawWords[0].text;
        for (size_t k = 1; k < i; k++) {
            candidate += ' ';
            candidate += rawWords[k].text;
        }

        auto it = verbTable.find(candidate);
        if (it == verbTable.end()) continue;

        for (VerbEntry& ve : it->second) {
            const VerbEntry* target = &ve;
            if (ve.isSynonym) {
                auto targetIt = verbTable.find(ve.synonymOf);
                target = (targetIt != verbTable.end()) ? findPlainEntry(targetIt->second, ve.synonymOf) : nullptr;
                if (!target) continue;
            }

            if (session.foundLevel < 1) session.foundLevel = 1;

            // real "if (!objects_loaded && (parse_verb_entry->flags &
            // VB_HAS_OBJ)) load_objects();" -- lazy, once per whole call,
            // the first time a matched verb has any rule with an
            // object-family token. Always called with session.caller's
            // own environment (real parse_user -- parseSentence()'s own
            // this_player(), parseMyRules()'s own explicit `user` arg).
            if (!session.objectsLoaded) {
                bool verbHasObj = std::any_of(target->nodes.begin(), target->nodes.end(),
                                               [](const VerbRuleNode& n) { return n.hasObjectToken; });
                if (verbHasObj) {
                    session.loaded = ParserPackage::loadObjects(
                        vm, session.caller, session.envArray ? &*session.envArray : nullptr,
                        session.nicks ? &*session.nicks : nullptr);
                    session.objectsLoaded = true;
                }
            }

            session.words.clear();
            SentenceWord verbWord;
            verbWord.text = candidate;
            verbWord.rawStart = rawWords[0].rawStart;
            verbWord.rawEnd = rawWords[i - 1].rawEnd;
            session.words.push_back(verbWord);
            for (size_t k = i; k < rawWords.size(); k++) session.words.push_back(rawWords[k]);

            session.matchedVerb = target;
            parseRulesFor(vm, session, restrictedHandler);
        }
    }
}

// real f_parse_my_rules()'s own non-call branch: the winning match's
// already-built "verb_rule" argument array (real best_result->res[3],
// the same try==3 naming variant we_are_finished() pre-builds for every
// match -- SentenceMatchResult::res's own comment), copied out and
// re-filtered for any object that got destructed as a side effect of a
// LATER candidate rule's own can_/direct_/do_ callbacks running before
// this call returns (real "if (arr->item[n].type == T_OBJECT &&
// arr->item[n].u.ob->flags & O_DESTRUCTED) { ...; arr->item[n] =
// const0u; }" -- the object was valid when we_are_finished() captured
// it, so this is a real, reachable case, not defensive-programming
// paranoia).
Value buildRuleArgsArray(const SentenceSession& session) {
    auto arr = std::make_shared<Array>();
    arr->items = session.bestResult->res[3].args;
    for (Value& v : arr->items) {
        if (auto* obp = std::get_if<std::shared_ptr<LpcObject>>(&v.data)) {
            if (!*obp || (*obp)->isDestructed()) v = Value(static_cast<int64_t>(0));
        }
    }
    return Value(arr);
}

} // namespace

Value ParserPackage::parseSentence(VM& vm, const std::shared_ptr<LpcObject>& caller, const std::string& sentence,
                                     bool debugFlag, const Value* envArray, const Value* nicks) {
    // real code's own check order: the "not known by the parser" guard
    // runs before anything else, including the debug-flag check below.
    if (!caller || !caller->hasParseInfo()) {
        throw LpcRuntimeError("parse_sentence: object is not known by the parser (call parse_init() first)");
    }
    // real f_parse_sentence()'s own `#else error("Parser debugging not
    // enabled. (compile with -DDEBUG or -DPARSE_DEBUG).\n");` -- this
    // driver has no such tracing at all, so that branch is always the
    // real one taken for a truthy debug argument.
    if (debugFlag) {
        throw LpcRuntimeError("parse_sentence: parser debugging not enabled (compile with -DDEBUG or -DPARSE_DEBUG)");
    }

    SentenceSession session;
    session.rawInput = sentence;
    session.caller = caller;
    // real `parse_env = (sp--)->u.arr;` -- only consulted by
    // loadObjects() (its own comment), which the verb-lookup loop below
    // lazily calls at most once per parseSentence() call.
    if (envArray) session.envArray = *envArray;
    // real "if (st_num_arg == 4) parse_nicks = (sp--)->u.map;" -- same
    // "only consulted by loadObjects()" shape as envArray above, plus
    // parseObj()'s own lazy expandNode() later in this same call
    // (SentenceSession::nicks's own comment has the full real citation).
    if (nicks) session.nicks = *nicks;

    // real "pi = parse_user->pinfo;", set for the duration of this call
    // (ParseInProgressGuard's own comment) -- parseSentence() itself
    // never checks this (real code's own guard is commented out here),
    // only sets it, so a parseMyRules() call reached through one of this
    // match's own can_/direct_/do_ callbacks correctly sees a parse
    // already in progress and errors, matching real behavior.
    ParseInProgressGuard guard;
    runParseMatch(vm, session, nullptr, verbs());

    if (session.bestMatchWeight != 0) {
        doTheCall(vm, session);
        return Value(static_cast<int64_t>(1));
    }
    return getTheError(vm, session);
}

Value ParserPackage::parseMyRules(VM& vm, const std::shared_ptr<LpcObject>& user,
                                    const std::shared_ptr<LpcObject>& restrictedHandler, const std::string& sentence,
                                    bool doTheCallFlag) {
    // real f_parse_my_rules()'s own two "not known by the parser" checks,
    // in real order: `user` (real (sp-2)->u.ob) first, then the calling
    // object itself (real current_object, this driver's `restrictedHandler`
    // -- see this method's own header comment for why it is exactly
    // real parse_restricted, threaded through as an explicit parameter
    // instead of a global).
    if (!user || !user->hasParseInfo()) {
        throw LpcRuntimeError("parse_my_rules: object is not known by the parser (call parse_init() first)");
    }
    if (!restrictedHandler || !restrictedHandler->hasParseInfo()) {
        throw LpcRuntimeError("parse_my_rules: object is not known by the parser (call parse_init() first)");
    }
    // real "if (pi) error(\"Illegal to call parse_sentence()
    // recursively.\n\");" -- unlike parse_sentence() itself, real
    // f_parse_my_rules() keeps this guard live (ParseInProgressGuard's
    // own comment: parse_sentence()'s matching disabled copy of the same
    // check, still commented out in real source, confirmed directly).
    if (parseInProgressFlag()) {
        throw LpcRuntimeError("parse_my_rules: illegal to call parse_sentence() recursively");
    }

    SentenceSession session;
    session.rawInput = sentence;
    session.caller = user;

    ParseInProgressGuard guard;
    runParseMatch(vm, session, restrictedHandler, verbs());

    if (session.bestMatchWeight != 0) {
        if (doTheCallFlag) {
            doTheCall(vm, session);
            return Value(static_cast<int64_t>(1));
        }
        // real "give them the info for the wildcard call" branch: return
        // the winning match's own res[3] ("verb_rule") argument array
        // instead of invoking anything.
        return buildRuleArgsArray(session);
    }
    return getTheError(vm, session);
}

} // namespace amlp

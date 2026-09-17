#pragma once
#include <optional>
#include <string>

namespace amlp {

// Per-dialect master/simul_efun/driver-object apply names. See
// src/dialect/instruct.md and src/apply/instruct.md Phase 1.4/1.15/1.16
// for the full design and the 2026-08-22 cross-driver comparison session
// that corrected several of these names against real LDMud/DGD source.
//
// Deliberately incomplete: this interface omits connectApply() and
// netDeadApply() on purpose. Both are recorded as unresolved -- a plain
// std::string return per concept does not fit DGD's three-way connect
// fork (telnet_connect/binary_connect/datagram_connect) or the fact that
// each dialect calls its disconnect-equivalent on a different receiver
// (FluffOS: the player object; LDMud: master; DGD: the persistent user
// object). src/apply/instruct.md's own open design note says explicitly
// to resolve that shape question before writing ApplyTable's dispatch
// code, not before adding the rest of this interface, so the settled
// methods are implemented here and those two are left for whoever
// resolves that note.
class BootApi {
public:
    virtual ~BootApi() = default;

    virtual std::string masterFile() const = 0;
    virtual std::optional<std::string> simulEfunFile() const = 0;
    virtual std::string logonApply() const = 0;
    virtual std::string compileObjectApply() const = 0;
    virtual std::string privsFileApply() const = 0;
    virtual std::string heartBeatErrorApply() const = 0;
    virtual bool hasAutoObject() const = 0;
    virtual std::optional<std::string> autoObjectFile() const = 0;

    // The master object's own trust-root UID apply. Real name differs by
    // dialect for the exact same concept: FluffOS "get_root_uid"
    // (applies.h APPLY_GET_ROOT_UID, master.c: "master_ob->uid =
    // set_root_uid(ret->u.string)"). LDMud renamed this to
    // "get_master_uid" in 3.2.1@40 (doc/master/get_master_uid's own
    // HISTORY line: "Introduced in 3.2.1@40 replacing get_root_uid().");
    // an earlier draft of this project's own docs used the superseded
    // FluffOS name for LDMud too, corrected 2026-08-22 against a real
    // LDMud 3.6.8 clone. This method exists to carry that corrected fact.
    virtual std::string masterUidApply() const = 0;

    // The real LDMud post-master-load boot callback (ROADMAP.md row
    // 1.7/1.8): "void inaugurate_master(int arg)", called once right
    // after the master object is loaded and its UID is set -- real
    // main.c:661-663's own "initialize_master_uid(); push_number(
    // inter_sp, 0); callback_master(STR_INAUGURATE, 1);". Real doc/
    // master/inaugurate_master's own words: "This function has to at
    // least set up the driverhooks to use" -- this is the real trigger
    // for secure/master/hooks.c's own addDriverHooks(). Genuinely
    // LDMud-only, not just a differently-spelled version of a universal
    // concept the way masterUidApply() is: real FluffOS's own
    // set_master() (master.c:88-141) only ever queries get_root_uid()/
    // get_backbone_uid(), and real FluffOS main.c's own boot sequence
    // calls preload_objects() (a C-level mechanism, not a single mudlib
    // callback) instead -- confirmed directly against the vendored real
    // FluffOS 2.9 reference source, not assumed. std::optional (unlike
    // masterUidApply() and friends) exactly because FluffOS/DGD
    // genuinely have nothing to return here -- std::nullopt means "this
    // dialect has no equivalent apply at all", distinct from "the
    // mudlib doesn't happen to define one" (already handled safely
    // either way by callFunction()'s own existing "undefined function
    // is not an error" convention).
    virtual std::optional<std::string> inaugurateMasterApply() const = 0;
};

} // namespace amlp

#pragma once
#include <optional>
#include <string>

namespace amlp {

class VM;
class BootApi;

// Fires BootApi::masterUidApply() on the currently loaded master object
// via VM::applyMaster() -- the real per-dialect boot-time UID query.
// FluffOS: "get_root_uid" (master.c's own "apply_master_ob
// (APPLY_GET_ROOT_UID, 0)"). LDMud: "get_master_uid" (renamed from
// get_root_uid in LDMud 3.2.1@40, doc/master/get_master_uid). This is
// the first real runtime caller of BootApi::masterUidApply() -- until
// now it was only exercised in isolation by src/dialect's own tests.
//
// Real semantics: a master that does not define this apply is not a
// boot failure in either dialect -- apply_master_ob() itself just
// returns 0/nil for an undefined function, silently -- mirrored here by
// returning std::nullopt rather than throwing. An exception thrown by
// the apply's own LPC body is caught and also treated as "no uid",
// matching this driver's existing applyMaster() call sites
// (ObjectManager::initPrivsForObject's own privs_file handling is the
// same shape). VM::applyMaster() throwing because no master is loaded
// at all is NOT swallowed here -- the caller is expected to have
// already loaded master successfully first, same as every other
// applyMaster() call site in this driver.
std::optional<std::string> queryMasterUid(VM& vm, const BootApi& bootApi);

} // namespace amlp

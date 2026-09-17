#pragma once

namespace amlp {

class VM;
class BootApi;

// Fires BootApi::inaugurateMasterApply() (real LDMud's own "void
// inaugurate_master(int arg)") on the currently loaded master object via
// VM::applyMaster(), arg=0 ("the mud just started, this is the first
// master of all", doc/master/inaugurate_master) -- the real trigger for
// a real LDMud master's own driver-hook setup (e.g. secure/master/
// hooks.c's own addDriverHooks()). See BootApi::inaugurateMasterApply()'s
// own comment for the full real-source citation (main.c:661-663) and
// why this is genuinely LDMud-only rather than a differently-spelled
// universal concept.
//
// A no-op under any dialect whose BootApi returns std::nullopt (real
// FluffOS/DGD have no equivalent apply at all). A master that does not
// define this apply under LDMud is not a boot failure either --
// callFunction()'s own existing "undefined function is not an error"
// convention already covers that silently. An exception thrown by the
// apply's own LPC body is caught and logged, not fatal to the rest of
// boot -- matching queryMasterUid()'s own established convention for
// this exact boot phase (MasterUidBoot.hpp).
//
// Split into its own reusable function, not left inline in main.cpp,
// for the same reason queryMasterUid() is: so a test harness that never
// runs the real main() can still exercise the real boot-sequence logic
// directly (see test_lexer.cpp's own inaugurate_master() regression
// tests).
void applyInaugurateMaster(VM& vm, const BootApi& bootApi);

} // namespace amlp

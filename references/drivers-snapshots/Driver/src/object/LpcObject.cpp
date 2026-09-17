#include "amlp/object/LpcObject.hpp"
#include <random>

namespace amlp {

LpcObject::LpcObject(std::string filename, std::shared_ptr<CompiledProgram> program,
                      bool fluffosDialect)
    : filename_(std::move(filename)), program_(std::move(program)) {
    // Object variable storage is sized once here, at construction, rather
    // than by each caller that constructs an LpcObject (ObjectManager's
    // loadObject()/cloneObject()): this way every LpcObject is correctly
    // sized the moment it exists, with nothing for a caller to remember.
    //
    // Filled with a real int64_t 0 per slot, not a default-constructed
    // Value{} (monostate). Real FluffOS's every declared object
    // variable, of whatever type, reads back as int64_t 0 in every
    // arithmetic/comparison/truthiness context until first assigned
    // (efuns_main.c/interpret.c's own svalue_t defaulting) -- but,
    // confirmed directly against real source (not the "no separate
    // unset state at all" claim this comment used to make): that 0 is
    // real FluffOS's own const0u, T_NUMBER tagged with the T_UNDEFINED
    // subtype, distinguishable from an explicitly-assigned 0 by
    // undefinedp()/nullp() specifically (see Value.hpp's own
    // isUndefined/makeUndefinedNumber() comments for the full
    // citation). Previously left as monostate here on an unverified
    // assumption that it "reads as 0" -- it does not: monostate fails
    // every arithmetic opcode (Add, IncDec, ...) that a real 0 would
    // silently succeed at. Confirmed live: std/Object.c's own
    // query_name() reading an unset __TrueName, and std/user/nmsh.c's
    // own add_history_cmd() doing "++__CmdNumber" on an unset counter,
    // both threw "unsupported operand types" against a real 0-defaulted
    // mudlib before that fix. `fluffosDialect` (default true, see this
    // constructor's own header comment) gates the isUndefined tag
    // itself: real LDMud has no equivalent concept, so a declared
    // object variable there is a plain, untagged 0, matching
    // undefinedp()/nullp() correctly returning false for it under that
    // dialect too.
    Value defaultVar = fluffosDialect ? makeUndefinedNumber() : Value(int64_t{0});
    variables_.resize(program_->objectVarNames.size(), defaultVar);

    // real object_t's own time_of_ref starts at creation time (nothing in
    // real object.c ever leaves it at a raw-zero epoch for a live
    // object) -- gives a freshly created object a sane clean_up()-
    // eligibility baseline of "just touched", not "untouched since the
    // Unix epoch".
    touchTimeOfRef();
}

// real reset_object()'s own "Be sure to update time first !" step
// (object.c:825-828): "ob->time_reset = current_time + time_to_reset/2 +
// (mp_int)random_number((uint32)time_to_reset/2);" -- and its own
// unconditional "ob->flags |= O_RESET_STATE;" at the function's end
// (object.c:884), folded into the same call here since every real
// caller (H_CREATE_* dispatch at load/clone time, and a real or virtual
// reset firing) wants both together. Matches real random_number()'s own
// [0, n) range (see EfunTable.cpp's "random" efun for the same
// std::uniform_int_distribution idiom).
void LpcObject::armReset(std::chrono::seconds timeToReset) {
    auto half = timeToReset / 2;
    static std::mt19937 rng(std::random_device{}());
    std::chrono::seconds jitter{0};
    if (half.count() > 0) {
        std::uniform_int_distribution<long long> dist(0, half.count() - 1);
        jitter = std::chrono::seconds(dist(rng));
    }
    timeReset_ = std::chrono::steady_clock::now() + half + jitter;
    resetState_ = true;
}

} // namespace amlp

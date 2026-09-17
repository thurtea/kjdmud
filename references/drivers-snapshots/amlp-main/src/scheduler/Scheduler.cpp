#include "amlp/scheduler/Scheduler.hpp"
#include "amlp/config/Config.hpp"
#include "amlp/dialect/LpcDialect.hpp"
#include "amlp/net/Server.hpp"
#include "amlp/object/LiveObjectRegistry.hpp"
#include "amlp/object/LpcObject.hpp"
#include "amlp/vm/VM.hpp"
#include <algorithm>
#include <atomic>
#include <iostream>
#include <thread>

namespace amlp {

namespace {
std::atomic<bool> g_shutdownRequested{false};
}

Scheduler::Scheduler(VM& vm)
    : vm_(vm), lastHeartbeat_(std::chrono::steady_clock::now()) {}

void Scheduler::requestShutdown() {
    g_shutdownRequested.store(true);
}

bool Scheduler::isShutdownRequested() {
    return g_shutdownRequested.load();
}

void Scheduler::run(Server& server, int maxIterations) {
    g_shutdownRequested.store(false);
    int iterations = 0;

    for (;;) {
        server.pollOnce();

        // backend.c: remove_destructed_objects() at the top of while(1).
        // No deferred-destruct list here, only the replace_program() half.
        vm_.processPendingReplacePrograms();

        // call_out.c: call_heart_beat() only when elapsed time crosses
        // HEARTBEAT_INTERVAL. Gate is here so tickHeartbeats() stays one cycle.
        auto now = std::chrono::steady_clock::now();
        if (now - lastHeartbeat_ >= kHeartbeatCycle) {
            lastHeartbeat_ = now;
            tickHeartbeats();
            // LDMud ALARM_TIME (config.h.in): reset/clean_up share
            // heart_beat granularity. FluffOS sweeps every 5 minutes
            // (backend.c:216); this matches LDMud's tighter default.
            tickResetsAndCleanup();
        }
        tickCallOuts();

        // Resume runAsync() coroutines parked on OpCode::Suspend.
        vm_.resumeReadyAsyncTasks(now);

        ++iterations;
        if (maxIterations > 0 && iterations >= maxIterations) break;
        if (g_shutdownRequested.load()) break;

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

int64_t Scheduler::newCallOutHandle() {
    // call_out.c new_call_out() encodes a ring-buffer slot. No ring
    // here (plain vector), so a monotonic counter is the unique handle.
    return nextHandle_++;
}

int64_t Scheduler::addCallOut(CallOutEntry entry) {
    entry.handle = newCallOutHandle();
    int64_t handle = entry.handle;
    callOuts_.push_back(std::move(entry));
    return handle;
}

namespace {
// call_out.c time_left(): whole seconds until due. dueAt - now, clamped to 0.
int64_t remainingSeconds(const std::chrono::steady_clock::time_point& dueAt) {
    auto now = std::chrono::steady_clock::now();
    if (dueAt <= now) return 0;
    return std::chrono::duration_cast<std::chrono::seconds>(dueAt - now).count();
}
} // namespace

int64_t Scheduler::removeCallOutByName(const std::shared_ptr<LpcObject>& owner, const std::string& function) {
    if (!owner) return -1;
    // remove_call_out(object_t*, const char*): match owner+name. A
    // closure-bound entry has no target and never matches the string form.
    for (auto it = callOuts_.begin(); it != callOuts_.end(); ++it) {
        if (!it->closure && it->target.lock() == owner && it->function == function) {
            int64_t remaining = remainingSeconds(it->dueAt);
            callOuts_.erase(it);
            return remaining;
        }
    }
    return -1;
}

int64_t Scheduler::removeCallOutByHandle(int64_t handle) {
    for (auto it = callOuts_.begin(); it != callOuts_.end(); ++it) {
        if (it->handle == handle) {
            int64_t remaining = remainingSeconds(it->dueAt);
            callOuts_.erase(it);
            return remaining;
        }
    }
    return -1;
}

void Scheduler::removeAllCallOutsForObject(const std::shared_ptr<LpcObject>& obj) {
    for (auto it = callOuts_.begin(); it != callOuts_.end();) {
        std::shared_ptr<LpcObject> owner =
            it->closure ? it->closure->owner.lock() : it->target.lock();
        bool matchesObj = obj && owner == obj;
        bool ownerGone = !owner || owner->isDestructed();
        if (matchesObj || ownerGone) {
            it = callOuts_.erase(it);
        } else {
            ++it;
        }
    }
}

int64_t Scheduler::findCallOutByName(const std::shared_ptr<LpcObject>& owner, const std::string& function) const {
    if (!owner) return -1;
    for (const auto& entry : callOuts_) {
        if (!entry.closure && entry.target.lock() == owner && entry.function == function) {
            return remainingSeconds(entry.dueAt);
        }
    }
    return -1;
}

int64_t Scheduler::findCallOutByHandle(int64_t handle) const {
    for (const auto& entry : callOuts_) {
        if (entry.handle == handle) return remainingSeconds(entry.dueAt);
    }
    return -1;
}

void Scheduler::setHeartbeatInterval(const std::shared_ptr<LpcObject>& obj, int64_t to) {
    if (!obj) return;

    // set_heart_beat(): to==0 removes the object from heart_beats[].
    if (to == 0) {
        obj->setHeartbeatInterval(0);
        auto it = std::find_if(heartbeats_.begin(), heartbeats_.end(),
            [&obj](const HeartbeatEntry& e) { return e.target.lock() == obj; });
        if (it != heartbeats_.end()) heartbeats_.erase(it);
        return;
    }

    auto it = std::find_if(heartbeats_.begin(), heartbeats_.end(),
        [&obj](const HeartbeatEntry& e) { return e.target.lock() == obj; });
    if (it != heartbeats_.end()) {
        // Already enabled: a negative update is rejected, interval unchanged.
        if (to < 0) return;
        obj->setHeartbeatInterval(static_cast<int>(to));
        it->ticksRemaining = static_cast<int>(to);
        return;
    }

    // Fresh enable: negative interval clamps to 1 (fire every cycle).
    int interval = to < 0 ? 1 : static_cast<int>(to);
    obj->setHeartbeatInterval(interval);
    heartbeats_.push_back(HeartbeatEntry{obj, interval});
}

void Scheduler::tickHeartbeats() {
    // Snapshot who fires this cycle before any LPC: heart_beat() may
    // call set_heart_beat() and invalidate heartbeats_ iterators.
    std::vector<std::shared_ptr<LpcObject>> due;
    for (auto it = heartbeats_.begin(); it != heartbeats_.end();) {
        auto obj = it->target.lock();
        if (!obj) {
            // Dead weak_ptr: equivalent of O_DESTRUCTED already pruned
            // from heart_beats[] at destruct time.
            it = heartbeats_.erase(it);
            continue;
        }
        if (--it->ticksRemaining < 1) {
            it->ticksRemaining = obj->heartbeatInterval();
            due.push_back(obj);
        }
        ++it;
    }

    for (auto& obj : due) {
        try {
            vm_.resetEvalCost();
            // backend.c:355-373: set this_player()/command_giver for the
            // call (walk the shadow chain; null unless commands enabled).
            std::shared_ptr<LpcObject> giver = obj;
            while (auto shadowed = giver->shadowing().lock()) giver = shadowed;
            if (!giver->commandsEnabled()) giver = nullptr;
            vm_.pushCommandGiver(giver);
            // ORIGIN_DRIVER (backend.c call_direct). call_out string-form
            // firing is ORIGIN_INTERNAL instead; they are not the same.
            try {
                vm_.callFunction(obj, "heart_beat", {});
            } catch (...) {
                vm_.popCommandGiver();
                throw;
            }
            vm_.popCommandGiver();
        } catch (const std::exception& e) {
            std::cerr << "[heart_beat] " << obj->filename() << ": " << e.what() << "\n";
        }
    }
}

namespace {
// H_RESET/H_CLEAN_UP: string hook or unset (fallback name). Closure
// form is unimplemented (zero corpus use). FluffOS has no hook here
// (fixed APPLY_RESET/APPLY_CLEAN_UP); the fallback name is what it calls.
std::string hookFunctionName(VM& vm, int hookNum, const char* fallback) {
    Value hookVal = vm.getDriverHook(hookNum);
    if (auto* name = std::get_if<std::string>(&hookVal.data)) return *name;
    return fallback;
}
} // namespace

void Scheduler::tickResetsAndCleanup() {
    constexpr int kHReset = 7;    // mudlib/sys/driver_hook.h
    constexpr int kHCleanUp = 8;
    // TIME_TO_RESET/TIME_TO_CLEAN_UP defaults (LDMud configure 1800/3600).
    // FluffOS uses the same formula (object.c:1898-1900); its value comes
    // from runtime config and is not independently confirmed here.
    constexpr std::chrono::seconds kTimeToReset{1800};
    constexpr std::chrono::seconds kTimeToCleanUp{3600};

    // FluffOS vs LDMud: a real reset() this cycle suppresses clean_up()
    // this cycle only under LDMud (backend.c:1402-1406 !bResetCalled).
    // FluffOS look_for_objects_to_swap() does not (backend.c:241/267).
    // Both latch clean_up eligibility from time_of_ref BEFORE reset()
    // runs, because reset() itself is an ordinary touch.
    LpcDialect dialect = dialectFromString(vm_.config().dialect());

    auto now = std::chrono::steady_clock::now();
    // Snapshot first: reset()/clean_up() may mutate objects like heart_beat().
    for (auto& obj : LiveObjectRegistry::all()) {
        if (obj->isDestructed()) continue;

        // Latched before reset() (backend.c:241 FluffOS / :1321 LDMud).
        bool readyForCleanUp = obj->willCleanUp() && (now - obj->timeOfRef()) > kTimeToCleanUp;

        // Due-check: skip a resetState() "virtual" reset (object.c:75-82);
        // only call reset() once something has touched the object.
        // Divergence: no per-cycle batching cap (LDMud/FluffOS performance
        // strategy); every object due this tick is processed this tick.
        bool resetCalledThisCycle = false;
        if (now >= obj->timeReset()) {
            if (obj->resetState()) {
                // Virtual reset: nothing touched this object, push the timer.
                obj->armReset(kTimeToReset);
            } else {
                resetCalledThisCycle = true;
                // "Be sure to update time first!" -- reset_object(), both drivers.
                obj->armReset(kTimeToReset);
                std::string resetFn = hookFunctionName(vm_, kHReset, "reset");
                if (vm_.functionExists(obj, resetFn)) {
                    try {
                        vm_.resetEvalCost();
                        vm_.callFunction(obj, resetFn, {}, Origin::Driver);
                    } catch (const std::exception& e) {
                        std::cerr << "[reset] " << obj->filename() << ": " << e.what() << "\n";
                    }
                } else {
                    // No reset() in the object: stop trying (object.c:1904
                    // FluffOS, object.c:869 LDMud).
                    obj->disableReset();
                }
                if (obj->isDestructed()) continue;
                // reset_object() always sets O_RESET_STATE (object.c:884
                // LDMud, object.c:1908 FluffOS).
                obj->setResetState(true);
            }
        }

        // Clean up when O_WILL_CLEAN_UP and time-since-ref elapsed.
        // LDMud only: skip if a real reset() just fired this cycle.
        bool suppressedBySameCycleReset = resetCalledThisCycle && dialect == LpcDialect::LdMud;
        if (readyForCleanUp && !suppressedBySameCycleReset) {
            // Clone (or replaced) -> 0, else prog->ref. Fixed 1 stands
            // in for the real ref count (LpcObject::isClone()).
            std::vector<Value> args{Value(obj->isClone() ? int64_t{0} : int64_t{1})};
            // Save O_RESET_STATE; calling clean_up() would otherwise clear
            // it as an ordinary touch (apply_low()). Restore after return.
            bool savedResetState = obj->resetState();
            Value result;
            try {
                vm_.resetEvalCost();
                result = vm_.callFunction(obj, hookFunctionName(vm_, kHCleanUp, "clean_up"), args, Origin::Driver);
            } catch (const std::exception& e) {
                std::cerr << "[clean_up] " << obj->filename() << ": " << e.what() << "\n";
            }
            if (obj->isDestructed()) continue;
            if (!isTruthy(result)) obj->setWillCleanUp(false);
            obj->setResetState(savedResetState);
        }
    }
}

void Scheduler::tickCallOuts() {
    auto now = std::chrono::steady_clock::now();
    // call_out.c: pull due entries out of the chain before invoking, so a
    // body that reschedules itself cannot corrupt the structure being walked.
    std::vector<CallOutEntry> due;
    for (auto it = callOuts_.begin(); it != callOuts_.end();) {
        if (it->dueAt <= now) {
            due.push_back(std::move(*it));
            it = callOuts_.erase(it);
        } else {
            ++it;
        }
    }

    for (auto& entry : due) {
        if (entry.closure) {
            // call_function_pointer(): a destructed closure owner fails
            // the call, isolated as this one call_out's error.
            try {
                vm_.resetEvalCost();
                vm_.callClosure(entry.closure, entry.args);
            } catch (const std::exception& e) {
                std::cerr << "[call_out] (closure): " << e.what() << "\n";
            }
            continue;
        }
        auto obj = entry.target.lock();
        if (!obj) continue; // real: "if (!ob || (ob->flags & O_DESTRUCTED)) { free_call(cop); }" -- silently dropped, not an error.
        try {
            vm_.resetEvalCost();
            // ORIGIN_INTERNAL (call_out.c apply(..., ORIGIN_INTERNAL)),
            // not ORIGIN_DRIVER. heart_beat firing above is ORIGIN_DRIVER.
            vm_.callFunction(obj, entry.function, entry.args, Origin::Internal);
        } catch (const std::exception& e) {
            std::cerr << "[call_out] " << obj->filename() << "::" << entry.function << "(): " << e.what() << "\n";
        }
    }
}

} // namespace amlp

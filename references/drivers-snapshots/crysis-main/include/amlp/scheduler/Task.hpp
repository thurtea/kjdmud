#pragma once
#include <coroutine>
#include <exception>
#include <utility>
#include "amlp/vm/Value.hpp"

namespace amlp {

// ROADMAP.md row 2.5's own first slice: a minimal, hand-rolled C++20
// coroutine task type, deliberately not a general-purpose executor --
// exactly enough machinery for one VM::runAsync() activation to
// suspend (via OpCode::Suspend co_await-ing VM's own timer awaiter,
// see VM.hpp's suspendFor()) and resume later, with the suspension
// correctly propagating up through any nested runAsync() call the same
// way real C++20 coroutine chaining always propagates through
// co_await. That propagation is the entire reason this row picked this
// design over the old TaskFrame-capture sketch that used to live in
// this module's own instruct.md: a manual sketch would have had to
// hand-detect and re-propagate a suspend sentinel at every nested call
// site itself; here the compiler-generated coroutine frame and this
// type's own final_suspend() below do it for free, for arbitrary
// nesting depth, with zero extra code at each call site beyond the one
// co_await already needed there anyway.
//
// Header-only and deliberately independent of Scheduler's own concrete
// class (only <coroutine> and Value.hpp) -- src/scheduler/CMakeLists.txt
// links `scheduler` against `vm`, not the reverse, so nothing under
// `include/amlp/vm/` may depend on Scheduler's own class without
// creating a real circular library dependency. See VM.hpp's own
// resumeReadyAsyncTasks()/suspendFor() comments for how VM avoids ever
// needing to include Scheduler.hpp for this row's own mechanism.
template <typename T>
class Task {
public:
    struct promise_type {
        // Explicit (if trivial) default constructor: without one, this
        // struct is a plain aggregate, and C++20's parenthesized
        // aggregate-init (P0960) lets the compiler try building it
        // positionally from runAsync()'s own argument list (preceded by
        // its implicit `this` for a non-static member coroutine) when
        // deciding how to construct the promise object -- silently
        // landing the VM& receiver into this struct's first member
        // (`value`, a Value) and failing to compile with a confusing
        // "no matching Value constructor" error nowhere near this file.
        // A user-declared constructor here, even an empty one, makes
        // this a non-aggregate, so the compiler correctly falls back to
        // ordinary default construction instead.
        promise_type() = default;

        T value{};
        std::exception_ptr error;
        // Who to resume when this Task finishes -- set by whichever
        // *other* runAsync() coroutine co_await's this one (Task's own
        // await_suspend() below), left null for a task driven directly
        // by an external resume() call (a fresh top-level task, or one
        // being woken back up by VM::resumeReadyAsyncTasks()).
        std::coroutine_handle<> continuation;

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        // Always starts suspended: runAsync() constructing a Task does
        // not itself begin executing any bytecode until something
        // (resume(), or a co_await from another coroutine) actually
        // drives it, matching this row's own "resume() starts a fresh
        // task" contract (see Task::resume() below).
        std::suspend_always initial_suspend() noexcept { return {}; }

        struct FinalAwaiter {
            bool await_ready() noexcept { return false; }
            // Real propagation step: resume whoever co_await'd this
            // Task, if anyone did, via symmetric transfer straight
            // into their own coroutine frame -- the exact mechanism
            // that makes suspension reached through an intervening
            // plain nested call correctly bubble all the way up to
            // the real top-level driver with no per-call-site manual
            // bookkeeping (see this file's own header comment).
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> h) noexcept {
                auto& promise = h.promise();
                if (promise.continuation) return promise.continuation;
                return std::noop_coroutine();
            }
            void await_resume() noexcept {}
        };
        FinalAwaiter final_suspend() noexcept { return {}; }

        void return_value(T v) { value = std::move(v); }
        // Matches every real per-call_out/per-heartbeat isolation
        // pattern already established in Scheduler.cpp: an LPC-level
        // runtime error inside this task's own body is captured here,
        // not left to unwind through resume() itself -- see VM.hpp's
        // resumeReadyAsyncTasks() comment for what is (and, honestly,
        // is not yet) done with it once captured.
        void unhandled_exception() { error = std::current_exception(); }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    explicit Task(handle_type h) : handle_(h) {}
    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;
    // Unconditionally destroys the coroutine frame, including one still
    // parked (e.g. in VM::pendingAsyncResumes_) -- a known, named limit
    // of this minimal first slice, not fixed here: nothing this row's
    // own scope creates a Task and abandons before driving it to
    // completion, but a future caller that does will leave a dangling
    // coroutine_handle in that queue. Real cancellation is out of scope
    // for this slice (not named in ROADMAP.md row 2.5's own 5-item
    // list) and should be a deliberate follow-on, not retrofitted here.
    ~Task() { if (handle_) handle_.destroy(); }

    bool done() const { return !handle_ || handle_.done(); }

    // Starts (the first call) or resumes (a later call, after this
    // task has been parked and then re-queued ready by
    // VM::resumeReadyAsyncTasks()) this task directly, without another
    // coroutine co_await-ing it. This is how a genuinely top-level task
    // is driven -- see this row's own regression tests for both shapes.
    void resume() { if (handle_ && !handle_.done()) handle_.resume(); }

    // Rethrows a captured LPC-level error (see unhandled_exception()
    // above) if this task's body ever threw one, otherwise returns its
    // real result. Only valid once done() is true.
    T takeResult() {
        if (handle_.promise().error) std::rethrow_exception(handle_.promise().error);
        return std::move(handle_.promise().value);
    }

    // Awaitable interface: lets one runAsync() coroutine co_await
    // another Task<Value> directly -- the OpCode::Call-to-an-async-
    // callee path (see VM.cpp's own runAsync() Call handling).
    bool await_ready() const noexcept { return handle_.done(); }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        handle_.promise().continuation = awaiting;
        // Symmetric transfer straight into the callee's own coroutine
        // body -- equivalent to calling resume() on it right here, but
        // without growing the real C++ stack the way a plain nested
        // call would (tail-call-shaped by construction, matching how
        // every other real C++20 coroutine chain avoids unbounded
        // native stack growth across deep await chains).
        return handle_;
    }
    T await_resume() { return takeResult(); }

private:
    handle_type handle_;
};

} // namespace amlp

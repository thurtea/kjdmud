# src/scheduler/ - call_out, heart_beat, Async Task Scheduler

## What lives here

| File | Role |
|------|------|
| `Scheduler.cpp` + `include/.../Scheduler.hpp` | `Scheduler::run()` event loop; `CallOutEntry`/`HeartbeatEntry` data; `tickCallOuts()`/`tickHeartbeats()`; handle-based and name-based remove. |

The Scheduler already implements real FluffOS-grounded semantics for call_out
and heart_beat as of 2026-08-07. All confirmed live.

## Files to read before touching this directory

- `include/amlp/scheduler/Scheduler.hpp` - full API
- Reference: `fluffos-2.9-ds2.08/call_out.c` - real call_out implementation
- Reference: `fluffos-2.9-ds2.08/backend.c` - `call_heart_beat()`, heartbeat
  interval, `kHeartbeatCycle` constant

## Phase 0 tasks

No scheduler stub gaps - `tickCallOuts()` and `tickHeartbeats()` are real and
confirmed live (2026-08-07). The Phase 0 work here is purely test coverage:

- `test/test_scheduler.cpp` must exist and cover:
  - call_out fires after correct delay
  - call_out removed by handle before firing
  - call_out removed by name before firing
  - self-rescheduling call_out fires correctly on second trigger
  - heartbeat fires after N cycles, interval respected
  - heartbeat disabled (interval = 0) does not fire
  - runtime error in a call_out callback does not stop subsequent call_outs

## Phase 2 tasks

### 2.5 - C++20 coroutine-based async task model

**Goal:** replace the current 50 ms `poll`-then-sleep `run()` loop with a
proper coroutine-driven event loop that allows LPC tasks to suspend without
blocking the whole server.

**What to build:**

1. **`Task` struct** - represents a suspended LPC execution:
   ```cpp
   struct Task {
       std::coroutine_handle<> handle;
       std::chrono::steady_clock::time_point resumeAt;
       std::shared_ptr<LpcObject> owner;
   };
   ```

2. **`Scheduler::suspend(Task& task, duration delay)`** - saves `task.handle`,
   sets `task.resumeAt = now + delay`, inserts into a priority queue sorted
   by `resumeAt`. Returns control to the event loop.

3. **`Scheduler::run()`** - becomes:
   ```
   while (!shutdownRequested) {
       now = steady_clock::now();
       fireReadyTasks(now);       // resume coroutines whose resumeAt <= now
       tickCallOuts();            // existing, unchanged
       tickHeartbeats();          // existing, unchanged
       server.pollOnce();         // existing, unchanged
       sleepUntilNextEvent(now);  // sleep only as long as needed
   }
   ```

4. **`call_out_future(delay)`** efun (Phase 2.7) - returns an awaitable that
   suspends the calling LPC coroutine for `delay` seconds. Called with
   `await call_out_future(5.0)`.

5. **Hydra parallel tasks (Phase 2.8)** - when two tasks' `owner` objects are
   provably disjoint (no shared inventory, no shared array/mapping references),
   both can be resumed concurrently using `std::jthread` worker threads. Each
   thread holds a snapshot of its task's object graph; on completion the main
   thread merges results. If any conflict is detected, the speculative task
   is rolled back (see `src/vm/instruct.md` Phase 1.12 for the rollback
   mechanism - same checkpoint logic applies here).

### 2.6 - LPC `async`/`await` keyword pair

When `VM::run()` hits the `Suspend` opcode (emitted for `await expr`):
1. Serialize the current VM stack frame into a `TaskFrame`.
2. Call `scheduler_->suspend(frame, delay)`.
3. Return a sentinel to the C++ caller (the coroutine yields here).

When `Scheduler::fireReadyTasks()` picks up the `Task`:
1. Restore the `TaskFrame` into a fresh `VM::run()` invocation.
2. Push the awaited result onto the stack.
3. Continue execution from the saved program counter.

## Key invariants

- `tickCallOuts()` and `tickHeartbeats()` must remain public and directly
  callable without real-time gating - tests drive them deterministically.
- A runtime error in one call_out/heartbeat must never prevent others from
  firing (the existing per-callback try/catch must be preserved in all
  refactoring).
- The coroutine scheduler (Phase 2.5) must preserve this isolation guarantee:
  one crashed coroutine does not abort the loop.
- `Scheduler::requestShutdown()` is a static signal - the run loop checks it
  once per iteration; do not change this to a member call or instance-level
  flag without updating all callers.

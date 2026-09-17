# src/persist/ - World Statedumps, Hotboot, Object Swapout (Phase 2a)

## Purpose

Persistent world state - AMLP exceeds DGD by supporting *both*
world-level snapshots and per-object `save_object()`/`restore_object()`
simultaneously. DGD forces you to choose one; AMLP supports both.

This is a **Phase 2a directory**. Create it only after Phase 1 is complete.

## Files to create

### `include/amlp/persist/StateSerializer.hpp`

```cpp
class StateSerializer {
public:
    // Serialize the full driver state to a CBOR binary file.
    // Includes: all live objects (filename + variables), all pending
    // call_outs, all heartbeat registrations, current time offset.
    bool dumpState(const std::string& path,
                   const ObjectManager& objects,
                   const Scheduler& scheduler);

    // Restore from a previously written dump.
    // Returns false if missing or corrupted.
    bool restoreState(const std::string& path,
                      ObjectManager& objects,
                      Scheduler& scheduler);
};
```

**Format:** CBOR (RFC 7049) via `nlohmann/json::to_cbor` / `from_cbor`.
Self-describing, compact, no external schema required.

### `include/amlp/persist/SwapManager.hpp`

```cpp
class SwapManager {
public:
    explicit SwapManager(const std::string& swapDir);

    // Write object variables to swapDir/filename.swap.
    bool swapOut(const LpcObject& obj);

    // Restore variables from the swap file.
    bool swapIn(LpcObject& obj);

    bool isSwapped(const std::string& filename) const;
    void purgeSwapFile(const std::string& filename);
};
```

Integration: `ObjectManager` calls `swapOut()` after an idle timeout
(`Config::swapIdleTimeout()`, default 300 s) and `swapIn()` before returning
the object from `lookupLoadedObject()`.

### `include/amlp/persist/HotbootManager.hpp`

```cpp
class HotbootManager {
public:
    // Prepare hotboot:
    // 1. dumpState() to statedump file
    // 2. Serialize live connection fds to a temp file
    // 3. exec() the new binary with --hotboot <tmpfile> arg
    static bool initiateHotboot(const std::string& newBinaryPath,
                                 const ObjectManager& objects,
                                 const Scheduler& scheduler,
                                 const Server& server);

    // Called by the new binary on startup when --hotboot is present.
    static bool resumeFromHotboot(const std::string& hotbootDataPath,
                                   ObjectManager& objects,
                                   Scheduler& scheduler,
                                   Server& server);
};
```

**fd inheritance:** set `O_CLOEXEC` on all internal fds except the accepted
player sockets; pass player socket fds via `SCM_RIGHTS` or as command-line
fd numbers to the new binary.

## New efuns (register in `src/efun`)

- `hotboot(string new_binary)` - triggers `HotbootManager::initiateHotboot()`
  (master-only, security checked)
- `dump_state()` - immediate statedump to `Config::statedumpFile()`
- `restore_state(string path)` - load a statedump (master-only)

## Integration in `main.cpp`

```cpp
// Periodic auto-dump
if (config.statedumpInterval() > 0 && timeSinceLastDump >= config.statedumpInterval()) {
    StateSerializer{}.dumpState(config.statedumpFile(), objects, scheduler);
}

// On startup: hotboot resume
if (argc > 2 && std::string(argv[1]) == "--hotboot") {
    HotbootManager::resumeFromHotboot(argv[2], objects, scheduler, server);
}
```

## CMakeLists.txt

```cmake
add_library(persist STATIC
    StateSerializer.cpp
    SwapManager.cpp
    HotbootManager.cpp
)
target_include_directories(persist PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(persist PUBLIC object scheduler net)
```

## Testing

`test/test_persist.cpp`:
- dumpState + restoreState round-trip for all Value kinds
- swapOut + swapIn preserves all variables exactly
- hotboot: fork, exec child with --hotboot arg, verify player connection survives

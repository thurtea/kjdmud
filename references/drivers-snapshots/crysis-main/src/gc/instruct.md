# src/gc/ - Generational Garbage Collector (Phase 3)

## Purpose

Replace the current `shared_ptr`-everywhere memory model with a generational
garbage collector tuned for LPC's object graph: many small short-lived arrays,
long-lived living objects, and occasional large mappings.

This is a **Phase 3 directory** - the single most invasive change in the
roadmap. Plan this carefully. Do not start until all Phase 0, 1, and 2 work
is complete and stable.

## Why this is needed

The current model (`shared_ptr<LpcObject>`, `shared_ptr<Array>`,
`shared_ptr<Mapping>`) works correctly for bring-up but has two known problems
at scale:

1. **Reference cycle leaks:** An object holding an array that holds a closure
   that holds the original object creates a reference cycle that `shared_ptr`
   cannot collect. In a running MUD with player inventories, this happens
   constantly.

2. **Copy pressure:** Every `Value` copy (stack push/pop, function call arg
   passing) copies the `shared_ptr` control block and atomically increments
   the refcount. Under heavy heartbeat/combat load, this becomes a bottleneck.

## Design

### Generation model

- **Gen 0 (nursery):** All newly allocated `Array`, `Mapping`, `Closure`, and
  `LpcObject` values go here. GC runs Gen 0 every N allocations (configurable).
- **Gen 1 (survivor):** Objects that survived one Gen 0 collection.
- **Gen 2 (old):** Long-lived objects (player objects, daemon objects). GC runs
  Gen 2 rarely (configurable interval or allocation pressure trigger).

### GC roots

- The VM evaluation stack (all `Value` slots)
- All `LpcObject::variables()` vectors
- The `ObjectManager` object cache
- The `Scheduler`'s `CallOutEntry::args` vectors

### Object header

Replace `shared_ptr<Array>` etc. with a raw pointer to a GC-managed header:
```cpp
struct GcObject {
    enum class Kind : uint8_t { Array, Mapping, Closure, LpcObj };
    Kind kind;
    uint8_t generation = 0;
    bool marked = false;
    bool pinned = false;   // true for C++ stack roots
};
```

`Value` stores a raw `GcObject*` tagged with the kind, not a `shared_ptr`.

### Write barrier

Any store to an old-generation object's fields that writes a pointer to a
young-generation object must be recorded in a "remembered set" so the GC
can find inter-generation pointers without scanning all old objects.

```cpp
void writeBarrier(GcObject* oldObj, GcObject* newValue);
```

Every `StoreObjectVar`, `StoreLocal`, and array/mapping mutation in `VM::run()`
calls `writeBarrier()`.

## Files to create

### `include/amlp/gc/GcHeap.hpp`

```cpp
class GcHeap {
public:
    static GcHeap& instance();

    // Allocate a GC-managed object of the given kind.
    GcObject* allocate(GcObject::Kind kind, size_t payloadBytes);

    // Register a root (VM stack slot, object variable slot, etc.)
    void addRoot(Value* root);
    void removeRoot(Value* root);

    // Trigger a collection of the given generation (0, 1, or 2).
    void collect(int maxGeneration = 0);

    // Called on every pointer write from old to young generation.
    void writeBarrier(GcObject* container, GcObject* newRef);

    // Statistics
    size_t liveObjects() const;
    size_t totalAllocated() const;
};
```

### `src/gc/GcHeap.cpp`

Mark-and-compact for Gen 0/1; mark-and-sweep for Gen 2 (to avoid moving
objects that C++ code holds raw pointers to, until all raw pointers are
replaced by GC handles).

## Migration path

The migration from `shared_ptr` to GC is the highest-risk operation in the
roadmap. Do it in layers:

1. **Layer 1 (no-op GC):** Introduce `GcHeap` as a thin wrapper around
   `shared_ptr` allocations. `GcObject` wraps a `shared_ptr` control block.
   No actual collection yet - just the API and test coverage.

2. **Layer 2 (array GC):** Migrate `shared_ptr<Array>` to GC-managed arrays.
   Keep `shared_ptr<LpcObject>` unchanged. Validate that array tests pass.

3. **Layer 3 (mapping + closure GC):** Migrate `shared_ptr<Mapping>` and
   `shared_ptr<Closure>`.

4. **Layer 4 (object GC):** Migrate `shared_ptr<LpcObject>`. This is the
   hardest step because `LpcObject` is referenced by `ObjectManager`'s cache,
   `InteractiveRegistry`, `LivingNameRegistry`, `Scheduler`, and `Server` -
   all need to switch from `shared_ptr` to GC handles.

5. **Layer 5 (generational):** Enable generation separation and write barriers.

## Testing

`test/test_gc.cpp`:
- Allocate a reference cycle; verify GC collects it (no leak after collection)
- Verify that a live stack root keeps an object alive across a collection
- Benchmark: measure allocation throughput before and after GC
  (target: no regression for the existing 374 tests; > 20% improvement on a
   10,000-array allocation benchmark)

## Key invariants

- **The GC must never collect a reachable object.** This is the invariant
  from which there is no recovery - it causes silent data corruption.
- Every layer of the migration must pass the full 374-test suite before
  the next layer begins.
- `GcHeap::collect()` must be safe to call from the main thread during the
  scheduler's idle period - never during active LPC code execution.

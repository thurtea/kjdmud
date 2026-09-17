# src/jit/ - LLVM JIT Backend (Phase 2c)

## Purpose

Compile hot LPC bytecode functions to native machine code via LLVM IR.
AMLP is the first LPC driver to offer LLVM-quality codegen as a
built-in first-class feature.

This is a **Phase 2c directory**. Create it only after Phase 1 is complete
and the interpreter passes all three dialect test suites.

## Prerequisites

- LLVM 17+ development libraries (`apt install llvm-17-dev` or custom build)
- `find_package(LLVM REQUIRED CONFIG)` in `driver/CMakeLists.txt`
- The interpreter must be 100% correct before JIT work begins - the JIT must
  produce bit-identical output for every existing test case.

## Files to create

### `include/amlp/jit/JitCompiler.hpp`

```cpp
class JitCompiler {
public:
    explicit JitCompiler(VM& vm, int threshold);

    using NativeFn = Value(*)(VM&, std::vector<Value>&);

    // Compile FunctionEntry to native. Returns nullptr on failure
    // (non-fatal: interpreter takes over).
    NativeFn compile(const FunctionEntry& fn, const CompiledProgram& prog);

    void recordCall(const FunctionEntry& fn);
    bool shouldCompile(const FunctionEntry& fn) const;
    NativeFn cachedNative(const FunctionEntry& fn) const;
};
```

### `src/jit/BytecodeToIr.cpp`

Translates a `FunctionEntry`'s bytecode to `llvm::Function`.

**OpCode → LLVM IR mapping (first-pass targets - pure arithmetic functions):**

| OpCode | IR approach |
|--------|------------|
| `PushInt` | `alloca Value`, store tag=int, store payload |
| `PushFloat` | same with float tag |
| `PushLocal` / `StoreLocal` | `alloca` per local slot, load/store |
| `Add` / `Sub` / `Mul` / `Div` | extract scalar, compute, re-box |
| `Eq` / `Lt` / `Lte` / etc. | `icmp`/`fcmp`, result as int Value |
| `JumpIfFalse` / `Jump` | LLVM basic-block branches |
| `Return` | LLVM `ret` |
| `Call` / `CallEfun` | call C++ `VM::callFunction()` trampoline (not JIT-inlined initially) |
| `PushObjectVar` / `StoreObjectVar` | call C++ `LpcObject::variables()` accessors |

**Value representation in IR:**
```llvm
%Value = type { i8, [24 x i8] }   ; tag byte + 24-byte payload
```
IR helper functions emitted as inlineable intrinsics:
`amlp_tag(Value*)`, `amlp_as_int(Value*)`,
`amlp_as_double(Value*)`, `amlp_box_int(i64)`,
`amlp_box_double(double)`.

### `src/jit/JitCompiler.cpp`

`JitCompiler::compile()` flow:
1. Create LLVM module + `BytecodeToIr::translate()`.
2. Run `llvm::PassBuilder` at `-O2` optimization level.
3. `LLJIT::addIRModule()` + `LLJIT::lookup()` → native function pointer.
4. Cache in `nativeCache_`.

## Integration in `VM::callFunction()`

```cpp
if (jit_ && jit_->shouldCompile(*entry)) {
    if (auto native = jit_->compile(*entry, program)) {
        return native(*this, args);
    }
}
jit_->recordCall(*entry);
// fall through to interpreter as before
```

`jit_` is a `std::unique_ptr<JitCompiler>` on `VM`, null by default,
created at boot when `Config::jitThreshold() > 0`.

## CMakeLists.txt

```cmake
option(AMLP_ENABLE_JIT "Enable LLVM JIT backend" OFF)
if(AMLP_ENABLE_JIT)
    find_package(LLVM REQUIRED CONFIG)
    llvm_map_components_to_libnames(llvm_libs support core orcjit native)

    add_library(jit STATIC JitCompiler.cpp BytecodeToIr.cpp)
    target_include_directories(jit PUBLIC ${CMAKE_SOURCE_DIR}/include ${LLVM_INCLUDE_DIRS})
    target_compile_definitions(jit PRIVATE ${LLVM_DEFINITIONS} AMLP_JIT_ENABLED)
    target_link_libraries(jit PUBLIC vm ${llvm_libs})

    target_link_libraries(amlp PRIVATE jit)
endif()
```

## Testing

`test/test_jit.cpp`:
- For each arithmetic-heavy VM test: enable JIT and assert identical output
- Verify JIT fallback: corrupt a bytecode vector in-memory so JIT fails;
  assert the interpreter runs the function correctly instead
- Benchmark: JIT-compiled inner loop must be ≥ 5x faster than interpreter
  on a tight `for` loop with integer arithmetic

# src/lsp/ - Language Server Protocol Server (Phase 2e)

## Purpose

Built-in LSP server for LPC: autocomplete, go-to-definition, hover, and
inline diagnostics in VS Code / neovim / any LSP-capable editor. No existing
LPC driver has this. It makes AMLP the first LPC runtime with a
first-class developer tooling story.

This is a **Phase 2e directory**. Create it only after Phase 1 is complete
(the compiler must be dialect-aware and produce high-quality diagnostics first).

## Activation

```bash
build/amlp --lsp etc/driver.cfg
```

In LSP mode the driver does NOT start the game server. Instead it:
1. Reads/writes LSP JSON-RPC on stdin/stdout (standard LSP transport).
2. Uses `ObjectManager` to compile `.c` files on demand.
3. Reports diagnostics, completions, and symbol definitions.

Alternatively, the LSP server can run on a separate port (`Config::lspPort()`)
alongside the game server for live introspection of a running mud.

## Files to create

### `include/amlp/lsp/LspServer.hpp`

```cpp
class LspServer {
public:
    explicit LspServer(Config& config, ObjectManager& objects, VM& vm);

    // Run the LSP JSON-RPC loop on stdin/stdout (or on a TCP port).
    void run();
    void runOnPort(int port);

private:
    // LSP request handlers
    nlohmann::json handleInitialize(const nlohmann::json& params);
    nlohmann::json handleTextDocumentDidOpen(const nlohmann::json& params);
    nlohmann::json handleTextDocumentDidChange(const nlohmann::json& params);
    nlohmann::json handleTextDocumentCompletion(const nlohmann::json& params);
    nlohmann::json handleTextDocumentHover(const nlohmann::json& params);
    nlohmann::json handleTextDocumentDefinition(const nlohmann::json& params);
    void publishDiagnostics(const std::string& uri, const std::string& source);

    Config& config_;
    ObjectManager& objects_;
    VM& vm_;
    // In-memory file cache: uri → current content (not yet saved to disk)
    std::unordered_map<std::string, std::string> fileContents_;
};
```

### `include/amlp/lsp/SymbolIndex.hpp`

```cpp
// Per-file symbol table built during compilation.
struct SymbolDef {
    std::string name;
    std::string file;
    int line = 0;
    int column = 0;
    enum Kind { Function, Variable, Efun, SimulEfun } kind;
};

class SymbolIndex {
public:
    void indexFile(const std::string& path, const CompiledProgram& prog);
    std::vector<SymbolDef> lookupDefinition(const std::string& name) const;
    std::vector<std::string> completionsAt(const std::string& file,
                                            int line, int col,
                                            const std::string& prefix) const;
    void removeFile(const std::string& path);
private:
    std::unordered_map<std::string, std::vector<SymbolDef>> byFile_;
    std::unordered_multimap<std::string, SymbolDef*> byName_;
};
```

The `SymbolIndex` is populated as `ObjectManager` compiles files. It is
shared between the LSP server and the game server (if running concurrently).

## LSP capabilities to implement (in priority order)

1. **`textDocument/publishDiagnostics`** - compile the file in memory, catch
   all `LpcRuntimeError` / `StructuredError` (Phase 2.20) exceptions, and
   send them as diagnostics. This is the single most valuable feature: instant
   red-squiggles for syntax and type errors.

2. **`textDocument/completion`** - suggest function names, efun names, and
   object paths at the cursor position. Use `SymbolIndex::completionsAt()` +
   `EfunTable::allNames()` (add this method to `EfunTable`).

3. **`textDocument/hover`** - show the function signature and doc comment
   (from the next line after the function declaration) when hovering over a
   call site.

4. **`textDocument/definition`** - jump to the definition of a function or
   variable. Uses `SymbolIndex::lookupDefinition()`.

5. **`textDocument/references`** - find all call sites of a function (requires
   a reverse-reference index, Phase 2e extension work).

## JSON-RPC transport

Use `nlohmann/json` (already a dependency) for all JSON encoding/decoding.

The LSP framing is:
```
Content-Length: <N>\r\n
\r\n
<N bytes of JSON>
```

Implement `LspServer::readMessage()` / `writeMessage()` using this framing
on stdin/stdout or a `Connection`-like TCP fd.

## CMakeLists.txt

```cmake
option(AMLP_ENABLE_LSP "Build LSP server" ON)
if(AMLP_ENABLE_LSP)
    add_library(lsp STATIC LspServer.cpp SymbolIndex.cpp)
    target_include_directories(lsp PUBLIC ${CMAKE_SOURCE_DIR}/include)
    target_link_libraries(lsp PUBLIC compiler vm object efun config)
    target_link_libraries(amlp PRIVATE lsp)
endif()
```

## Testing

`test/test_lsp.cpp`:
- Send a `textDocument/didOpen` with a file containing a syntax error;
  assert the expected diagnostic is published.
- Send `textDocument/completion` at a position after `write(`;
  assert `write`, `writef`, `writev` appear in completions.
- Send `textDocument/definition` on a call to a local function;
  assert the definition location is the function's declaration line.

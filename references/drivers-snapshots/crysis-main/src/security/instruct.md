# src/security/ - Security Model: privs_file, UID/GID Trust (Phase 3)

## Purpose

Full `privs_file` / uid/gid object trust hierarchy with filesystem jails per
object domain and explicit capability grants for `call_other` across trust
boundaries.

This is a **Phase 3 directory**. Create it only after all Phase 0, 1, and 2
work is complete.

## Background

Real FluffOS has a `privs_file` apply on the master object: whenever an object
tries to perform a privileged operation (read/write a file outside its own
domain, call a protected function on another object, clone a protected
blueprint), the master is asked whether to allow it. This driver currently
does not enforce these checks - the master apply is wired in but not called
at the right security decision points.

LDMud has a more elaborate trust model with root/backbone UIDs and per-object
effective-uid tracking. DGD has path-based read/write permission callbacks on
the driver object.

AMLP's goal: **one security model that covers all three dialects**, with
the dialect's boot API mapping its own permission callbacks to the shared
enforcement layer.

## Files to create

### `include/amlp/security/SecurityManager.hpp`

```cpp
class SecurityManager {
public:
    explicit SecurityManager(VM& vm, const BootApi& bootApi);

    // File operations
    bool allowRead(const LpcObject& caller, const std::string& path) const;
    bool allowWrite(const LpcObject& caller, const std::string& path) const;
    bool allowExec(const LpcObject& caller, const std::string& path) const;

    // Object operations
    bool allowClone(const LpcObject& caller, const std::string& blueprint) const;
    bool allowCallOther(const LpcObject& caller,
                        const LpcObject& target,
                        const std::string& function) const;
    bool allowDestruct(const LpcObject& caller, const LpcObject& target) const;

    // Privilege
    std::string getUid(const LpcObject& obj) const;
    std::string getEuid(const LpcObject& obj) const;
    bool isPrivileged(const LpcObject& obj) const;
};
```

### `src/security/SecurityManager.cpp`

For FluffOS dialect: call `master()->privs_file(path)` to get the privilege
string for a path, then compare against the caller's own privilege string.

For LDMud dialect: call `master()->valid_read(path, uid, "read", caller)`.

For DGD dialect: call `driver()->path_read(path)` and `path_write(path)`.

All three map to the same enforcement points (file operations, call_other,
clone, destruct) but route through different boot-API apply names.

## Enforcement integration points

After Phase 3, add security checks at:

1. **`ObjectManager::compile()`** - `allowRead(caller, filename)`
2. **`ObjectManager::cloneObject()`** - `allowClone(caller, blueprint)`
3. **`ObjectManager::destructObject()`** - `allowDestruct(caller, target)`
4. **`VM::callFunction()`** cross-object - `allowCallOther(caller, target, fn)`
5. **File efuns** (`read_file`, `write_file`, `append_file`, `get_dir`,
   `read_bytes`, `write_bytes`, `rename`, `rm`, `mkdir`, `rmdir`)
   - `allowRead` / `allowWrite` before every file syscall

## Filesystem jail

Each object's domain is determined by its path prefix:
- `/secure/` - admin domain (only master/simul_efun can call into these)
- `/domains/PrisonIsland/` - PrisonIsland domain
- `/std/` - standard library domain (readable by all, writable by nobody)

The `SecurityManager` enforces that an object in domain A cannot read/write
files in domain B without an explicit grant. Grants are declared in the
master object's `privs_file()` return value.

## New efuns (register in `src/efun`)

- `query_privs(object ob)` - return the privilege string of `ob`
- `set_privs(object ob, string privs)` - set privilege (master-only)
- `getuid(object ob)` / `geteuid(object ob)` - LDMud-style UID queries
- `seteuid(string uid)` - set effective UID of calling object
- `export_uid(object ob)` - LDMud: transfer own euid to `ob`
- `suid(object ob)` - LDMud: set-UID flag

## CMakeLists.txt

```cmake
add_library(security STATIC SecurityManager.cpp)
target_include_directories(security PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(security PUBLIC object apply config dialect)
target_link_libraries(amlp PRIVATE security)
```

## Testing

`test/test_security.cpp`:
- Object in `/domains/A/` cannot read a file in `/domains/B/` without a grant
- Object in `/secure/` can be called by master but not by a player object
- `clone_object("/secure/master")` by a non-master object throws a security error
- `allowCallOther` denies calls to `nomask` functions from unprivileged callers

## Key invariants

- Security checks happen BEFORE the operation, not after.
- A failed security check throws `LpcRuntimeError("Access denied: ...")` -
  never silently succeeds.
- The master object itself is exempt from all security checks (it is the
  authority, not a subject of the authority system).
- Security must be a compile-time option (`AMLP_ENABLE_SECURITY`) that
  defaults to OFF during development and ON for production builds, so the
  existing test suite can run without a correctly configured master object.

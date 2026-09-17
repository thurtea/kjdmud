#pragma once
#include <string>
#include <vector>
#include "amlp/vm/Value.hpp"

struct sqlite3;
struct sqlite3_stmt;

namespace amlp {

// Real db_* efun family, ported from LDMud's own pkg-mysql.c and
// doc/efun/db_* -- confirmed the real evidence source for ROADMAP.md row
// 2.15, not real FluffOS: core-lib's own README.md states directly it
// "targets the LDMud driver", and every real call site this project's
// only corpus evidence for this row (core-lib's dataServices/*.c,
// secure/simulated-efuns/database.c) uses is exactly this LDMud family
// (db_connect/db_exec/db_fetch/db_close/db_error/db_handles/
// db_conv_string) -- confirmed by direct signature/semantics match
// against temp/ldmud/src/pkg-mysql.c, not the FluffOS db package (which
// this project also has vendored, temp/reference/fluffos-2.9-ds2.08/
// packages/db.c: a completely different signature shape -- db_connect
// takes a host argument LDMud's never had, db_exec returns rows-affected
// or an error *string* rather than a handle-or-zero, db_fetch is
// row-indexed rather than sequential -- and has no db_error/db_handles/
// db_conv_string efuns at all, no SQLite backend of any kind, only mSQL/
// MySQL). Backed by SQLite instead of a live MySQL server -- a
// deliberate driver-side engine substitution (ROADMAP.md row 2.15's own
// scoping note: "SQLite3 is a far lighter dependency than OpenSSL or
// LLVM"), not something any corpus asks for by name; the LPC-visible
// efun names/signatures/return shapes below are real, cited LDMud
// fidelity, only the storage engine underneath differs.
//
// First-slice scope, bounded to exactly what core-lib's real call sites
// use: db_connect, db_exec, db_fetch, db_close, db_error, db_handles,
// db_conv_string. Real LDMud also has db_affected_rows/db_insert_id/
// db_coldefs -- confirmed zero call sites for any of the three anywhere
// in core-lib, this project's only real corpus evidence for this row --
// not built this slice, left for a separately-scoped follow-on if real
// evidence for them ever appears.
//
// Handle allocation: a monotonic counter, matching SocketRegistry's own
// established precedent in this driver (net/instruct.md's own stated
// invariant for socket handles) rather than real LDMud's own chained-
// list "first free slot" allocator (pkg-mysql.c's own
// allocate_new_dat()) -- a real implementation detail with zero
// LPC-visible contract depending on it: no real call site anywhere in
// core-lib inspects a handle's numeric *value*, only its truthiness and
// identity across calls.
//
// Privilege gating is NOT this class's job -- every real db_* call site
// in pkg-mysql.c calls check_privilege(name, MY_TRUE, sp) first, which
// hard-errors on denial; EfunTable.cpp's own registrations call
// VM::privilegeViolation("mysql", {efun name}) the same way this
// driver's other privilege_violation()-gated efuns already do (see
// input_to's own registration), since VM& is not available here.
class DbRegistry {
public:
    // int db_connect(string database, string|void user, string|void password)
    // Real f_db_connect()/v_db_connect(): user/password are accepted for
    // real MySQL auth and have no SQLite equivalent (SQLite has no
    // authentication layer) -- accepted here but silently unused, so a
    // mudlib written against the real efun's varargs signature (core-lib's
    // own secure/simulated-efuns/database.c calls db_connect(database,
    // user, password) with all 1/2/3-arg forms) still compiles and runs
    // rather than being rejected for passing arguments this backend
    // cannot use. <database> is the sqlite3 database file path.
    //
    // Real: "If the database does not exist or the server is NOT
    // started, a runtime-error is raised." SQLite has no separate
    // server to be "not started" and auto-creates a missing database
    // file by design (sqlite3_open()'s own well-known default behavior)
    // -- this driver keeps that SQLite-native auto-create behavior
    // rather than forcing an artificial "must already exist" check no
    // real SQLite caller would expect; only a genuine sqlite3_open_v2()
    // failure (bad path, permission denied, out of memory, ...) raises,
    // matching the real efun's own "raises a runtime-error on failure"
    // contract at the point where this backend can actually fail the
    // same way.
    //
    // <database> is a raw OS filesystem path, not run through this
    // driver's own LPC virtual-filesystem valid_read()/valid_write()
    // jail -- matching real db_connect()'s own contract exactly: real
    // LDMud's <database> argument is a MySQL database *name* resolved
    // by the MySQL server process itself, never touching LDMud's own
    // mudlib-relative file-access layer either. A mudlib that wants to
    // sandbox where its own db_connect() calls may point is expected to
    // enforce that itself before calling the efun (core-lib's own real
    // precedent: every real call site goes through its own
    // canAccessDatabase()-gated wrapper in secure/simulated-efuns/
    // database.c, never calling the raw efun directly from application
    // code).
    static int connect(const std::string& database, const std::string& user, bool hasUser,
                        const std::string& password, bool hasPassword);

    // int db_exec(int handle, string statement)
    // Real f_db_exec(): returns the handle again on success, 0 if the
    // statement itself was bad SQL (a real error in query syntax/
    // semantics, not a connection failure). A SELECT-shaped statement
    // (sqlite3_column_count() > 0 after prepare) leaves the prepared
    // statement un-stepped, pending for fetch() below to walk row by
    // row -- matching real f_db_exec()'s own "initiate a row-by-row
    // transfer" via mysql_use_result() without consuming any row itself.
    // Any other statement (INSERT/UPDATE/DELETE/CREATE/...) is stepped
    // to completion immediately, since nothing will ever call fetch()
    // for it. Throws LpcRuntimeError("illegal handle for database") for
    // an unknown/closed handle, matching real errorf("Illegal handle for
    // database.\n") -- every real db_* call site in pkg-mysql.c hard-
    // errors on an unknown handle, this is not a soft-fail case.
    static int exec(int handle, const std::string& statement);

    // mixed db_fetch(int handle)
    // Real f_db_fetch(): one row at a time from the most recent exec()'s
    // pending result set, 0 (Value{}, this driver's own "absent means
    // the LPC 0" convention) once exhausted or if exec() left nothing
    // pending (the last statement was not a SELECT). Row values: real
    // MySQL's own mysql_fetch_row() returns every column as raw text
    // regardless of its underlying SQL type (the C API has no other
    // form) -- confirmed directly, f_db_fetch()'s own loop calls
    // put_c_string() unconditionally for every non-NULL cell, never a
    // numeric svalue. This driver preserves that exact real quirk:
    // every non-NULL column comes back as a formatted string, not a
    // native SQLite int64/double, even though SQLite itself could
    // distinguish them -- a NULL column becomes Value(int64_t{0}),
    // matching real put_c_string()'s own implicit "else leave the
    // pre-zeroed array slot as 0" for a NULL MySQL cell.
    static Value fetch(int handle);

    // int db_close(int handle)
    // Real: returns the handle number on success. Finalizes any pending
    // fetch statement first -- a real, unavoidable local cleanup step
    // this driver's own sqlite3_stmt lifetime requires explicitly (the
    // real MySQL client library's mysql_free_result() plays the same
    // role inside remove_dat(), just not surfaced as a separate step
    // there).
    static int close(int handle);

    // string|int db_error(int handle)
    // Real f_db_error(): the last error string for this handle, or 0 (a
    // real T_NUMBER 0, not an empty string) if the last operation
    // succeeded. Value{} (monostate) represents that 0 here, matching
    // this driver's own established convention used throughout
    // Value.hpp.
    static Value error(int handle);

    // int *db_handles()
    // Real: every open handle, "sorted... last used handle first, ...
    // longest unused last." This driver returns them in ascending
    // numeric (insertion) order instead -- a real, deliberate departure
    // from that documented LRU order (see this class's own header
    // comment): this project's only real corpus evidence for this efun,
    // core-lib's secure/simul_efun.c "map(efun::db_handles(), #'db_close)",
    // closes every returned handle regardless of order, so no real call
    // site anywhere this project has evidence for depends on it.
    static std::vector<int> handles();

    // string db_conv_string(string str)
    // Real f_db_conv_string(): calls mysql_escape_string(), which
    // backslash-escapes '/"/\/NUL/control characters for MySQL's own
    // string-literal syntax. This driver escapes for SQLite's own
    // string-literal syntax instead (doubling a single quote, the only
    // character with special meaning inside a SQLite '...' literal) --
    // a deliberate, *necessary* divergence from the real byte-for-byte
    // MySQL escaping convention: this helper's entire real purpose is
    // producing a string safe to embed in whatever backend is actually
    // executing the query, and a MySQL-shaped backslash escape is not
    // syntactically meaningful, safe SQLite input.
    static std::string convString(const std::string& str);

    static bool exists(int handle);

    // Test-only: drops every open connection (closing each sqlite3*/
    // finalizing any pending statement first) and resets the handle
    // counter. Nothing in ordinary driver operation calls this --
    // SocketRegistry has no equivalent and this driver's existing tests
    // never reset it either, but this registry's handles are visible to
    // LPC via db_handles(), and the process-wide test binary shares one
    // instance across every test case in test_lexer.cpp, so leaving
    // stale-but-empty registry entries from an earlier test case's
    // scratch databases would leak into a later test's own db_handles()
    // read; called only from the new db_* tests below, immediately
    // before each one's own setup, not from any pre-existing test.
    static void resetForTests();
};

}  // namespace amlp

#include "amlp/efun/DbRegistry.hpp"
#include "amlp/core/Errors.hpp"
#include <sqlite3.h>
#include <algorithm>
#include <unordered_map>
#include <vector>

namespace amlp {

namespace {

struct DbConn {
    sqlite3* db = nullptr;
    sqlite3_stmt* pending = nullptr;  // set by exec() for a SELECT-shaped statement; walked by fetch()
    std::string lastError;           // empty means "last operation succeeded" (db_error() returns 0)
};

// Global table of every live db_* connection -- mirrors SocketRegistry's
// own "global map, no VM reference needed" shape (see this class's own
// header comment). Handles are 1-based (0 is never a valid handle,
// matching real LDMud: db_connect() failure paths and db_exec()/
// db_fetch() failure returns all use plain 0, which must stay
// unambiguous from any real handle).
std::unordered_map<int, DbConn>& registry() {
    static std::unordered_map<int, DbConn> table;
    return table;
}

int& nextHandleCounter() {
    static int next = 1;
    return next;
}

void finalizePending(DbConn& conn) {
    if (conn.pending) {
        sqlite3_finalize(conn.pending);
        conn.pending = nullptr;
    }
}

// Real f_db_fetch()'s own column-value convention: every non-NULL cell
// becomes a string (see DbRegistry.hpp's own fetch() comment for why),
// regardless of SQLite's actual per-cell storage class.
std::string formatColumnAsText(sqlite3_stmt* stmt, int col) {
    switch (sqlite3_column_type(stmt, col)) {
        case SQLITE_INTEGER:
            return std::to_string(sqlite3_column_int64(stmt, col));
        case SQLITE_FLOAT: {
            // Matches how a real MySQL client formats a FLOAT/DOUBLE
            // column as text: a plain decimal representation, no
            // trailing zero-padding beyond what's needed.
            double d = sqlite3_column_double(stmt, col);
            std::string s = std::to_string(d);
            // Trim trailing zeros (but keep at least one digit after '.').
            if (s.find('.') != std::string::npos) {
                size_t last = s.find_last_not_of('0');
                if (s[last] == '.') ++last;
                s.erase(last + 1);
            }
            return s;
        }
        case SQLITE_TEXT: {
            const unsigned char* text = sqlite3_column_text(stmt, col);
            int len = sqlite3_column_bytes(stmt, col);
            return text ? std::string(reinterpret_cast<const char*>(text), static_cast<size_t>(len)) : std::string();
        }
        case SQLITE_BLOB: {
            const void* blob = sqlite3_column_blob(stmt, col);
            int len = sqlite3_column_bytes(stmt, col);
            return blob ? std::string(reinterpret_cast<const char*>(blob), static_cast<size_t>(len)) : std::string();
        }
        default:
            return std::string();
    }
}

}  // namespace

int DbRegistry::connect(const std::string& database, const std::string& /*user*/, bool /*hasUser*/,
                         const std::string& /*password*/, bool /*hasPassword*/) {
    sqlite3* db = nullptr;
    int rc = sqlite3_open_v2(database.c_str(), &db,
                              SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) {
        std::string msg = db ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
        if (db) sqlite3_close(db);
        throw LpcRuntimeError(msg);
    }

    int handle = nextHandleCounter()++;
    DbConn conn;
    conn.db = db;
    registry()[handle] = std::move(conn);
    return handle;
}

int DbRegistry::exec(int handle, const std::string& statement) {
    auto it = registry().find(handle);
    if (it == registry().end()) {
        throw LpcRuntimeError("illegal handle for database");
    }
    DbConn& conn = it->second;

    finalizePending(conn);

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(conn.db, statement.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        // Real f_db_exec(): "just an error in the SQL-statement" ->
        // put_number(sp, 0), not a hard error -- the connection itself
        // is still fine, only this one statement failed.
        conn.lastError = sqlite3_errmsg(conn.db);
        if (stmt) sqlite3_finalize(stmt);
        return 0;
    }

    if (sqlite3_column_count(stmt) > 0) {
        // SELECT-shaped: leave un-stepped for fetch() to walk.
        conn.pending = stmt;
        conn.lastError.clear();
        return handle;
    }

    // Not a SELECT: execute immediately, nothing will call fetch() for it.
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        conn.lastError = sqlite3_errmsg(conn.db);
        return 0;
    }
    conn.lastError.clear();
    return handle;
}

Value DbRegistry::fetch(int handle) {
    auto it = registry().find(handle);
    if (it == registry().end()) {
        throw LpcRuntimeError("illegal handle for database");
    }
    DbConn& conn = it->second;

    if (!conn.pending) {
        return Value{};  // real "no more results / nothing pending" -> 0
    }

    int rc = sqlite3_step(conn.pending);
    if (rc == SQLITE_ROW) {
        int cols = sqlite3_column_count(conn.pending);
        auto row = std::make_shared<Array>();
        row->items.reserve(static_cast<size_t>(cols));
        for (int i = 0; i < cols; ++i) {
            if (sqlite3_column_type(conn.pending, i) == SQLITE_NULL) {
                row->items.emplace_back(int64_t{0});
            } else {
                row->items.emplace_back(formatColumnAsText(conn.pending, i));
            }
        }
        return Value(row);
    }

    // SQLITE_DONE (no more rows) or an error mid-fetch -- both end the
    // pending walk the same way real f_db_fetch() does on NULL
    // mysql_fetch_row() (it does not distinguish "done" from "error"
    // either at this call site).
    if (rc != SQLITE_DONE) {
        conn.lastError = sqlite3_errmsg(conn.db);
    }
    finalizePending(conn);
    return Value{};
}

int DbRegistry::close(int handle) {
    auto it = registry().find(handle);
    if (it == registry().end()) {
        throw LpcRuntimeError("illegal handle for database");
    }
    finalizePending(it->second);
    sqlite3_close(it->second.db);
    registry().erase(it);
    return handle;
}

Value DbRegistry::error(int handle) {
    auto it = registry().find(handle);
    if (it == registry().end()) {
        throw LpcRuntimeError("illegal handle for database");
    }
    if (it->second.lastError.empty()) return Value{};
    return Value(it->second.lastError);
}

std::vector<int> DbRegistry::handles() {
    std::vector<int> result;
    result.reserve(registry().size());
    for (const auto& [handle, conn] : registry()) result.push_back(handle);
    std::sort(result.begin(), result.end());
    return result;
}

std::string DbRegistry::convString(const std::string& str) {
    std::string out;
    out.reserve(str.size());
    for (char c : str) {
        if (c == '\'') out += "''";
        else out += c;
    }
    return out;
}

bool DbRegistry::exists(int handle) {
    return registry().count(handle) > 0;
}

void DbRegistry::resetForTests() {
    for (auto& [handle, conn] : registry()) {
        finalizePending(conn);
        sqlite3_close(conn.db);
    }
    registry().clear();
    nextHandleCounter() = 1;
}

}  // namespace amlp

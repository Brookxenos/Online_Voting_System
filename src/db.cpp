#include "db.h"

#include <stdexcept>
#include <iostream>

//
// SQLite C API declarations.
//
// Only the runtime library (libsqlite3.so.0) is present in this environment;
// there are no development headers, so the small, stable subset of the API
// that this project needs is declared here verbatim from sqlite3.h.
//

extern "C" {

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

int sqlite3_open_v2(const char *filename, sqlite3 **ppDb, int flags, const char *zVfs);
int sqlite3_close_v2(sqlite3*);
int sqlite3_exec(sqlite3*, const char *sql, void (*cb)(void*,int,char**,char**), void*, char **errmsg);
int sqlite3_prepare_v2(sqlite3*, const char *zSql, int nByte, sqlite3_stmt **ppStmt, const char **pzTail);
int sqlite3_bind_text(sqlite3_stmt*, int, const char*, int, void(*)(void*));
int sqlite3_bind_null(sqlite3_stmt*, int);
static void bindParams(sqlite3_stmt* stmt, const std::vector<DbValue>& binds) {
    for (size_t i = 0; i < binds.size(); ++i) {
        const DbValue& b = binds[i];
        int idx = (int)(i + 1);
        if (b.isNull) sqlite3_bind_null(stmt, idx);
        else sqlite3_bind_text(stmt, idx, b.val.c_str(), (int)b.val.size(), SQLITE_TRANSIENT);
    }
}
int sqlite3_step(sqlite3_stmt*);
int sqlite3_finalize(sqlite3_stmt*);
int sqlite3_column_count(sqlite3_stmt*);
const char *sqlite3_column_name(sqlite3_stmt*, int);
int sqlite3_column_type(sqlite3_stmt*, int);
long long sqlite3_column_int64(sqlite3_stmt*, int);
const unsigned char *sqlite3_column_text(sqlite3_stmt*, int);
const char *sqlite3_errmsg(sqlite3*);
long long sqlite3_last_insert_rowid(sqlite3*);
void sqlite3_free(void*);

#define SQLITE_OK           0
#define SQLITE_ROW          100
#define SQLITE_DONE         101
#define SQLITE_OPEN_READWRITE 0x00000002
#define SQLITE_OPEN_CREATE    0x00000004
#define SQLITE_FULLURI        0x00001000
#define SQLITE_NULL         5
#define SQLITE_TRANSIENT    ((void(*)(void*))-1)

} // extern "C"

Database::Database(const std::string& dbFile) {
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_FULLURI;
    if (sqlite3_open_v2(dbFile.c_str(), (sqlite3**)&db_, flags, nullptr) != SQLITE_OK) {
        db_ = nullptr;
        throw std::runtime_error("Cannot open database file: " + dbFile);
    }
    // WAL journaling: concurrent readers with a serialized writer, and
    // crash-safe commits.
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
    exec("PRAGMA busy_timeout=5000;");
    exec("PRAGMA synchronous=NORMAL;");
}

Database::~Database() {
    if (db_) { sqlite3_close_v2((sqlite3*)db_); db_ = nullptr; }
}

void Database::exec(const std::string& sql) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    char* err = nullptr;
    if (sqlite3_exec((sqlite3*)db_, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : sqlite3_errmsg((sqlite3*)db_);
        if (err) sqlite3_free(err);
        throw std::runtime_error("SQL error: " + msg + " — while running: " + sql);
    }
}

void Database::exec(const std::string& sql, const std::vector<DbValue>& binds) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("SQL prepare error: ") + sqlite3_errmsg((sqlite3*)db_) + " — " + sql);
    }
    bindParams(stmt, binds);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        throw std::runtime_error(std::string("SQL exec error: ") + sqlite3_errmsg((sqlite3*)db_) + " — " + sql);
    }
}

std::vector<Database::Row> Database::query(const std::string& sql,
                                           const std::vector<DbValue>& binds) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    std::vector<Row> out;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2((sqlite3*)db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("SQL prepare error: ") + sqlite3_errmsg((sqlite3*)db_) + " — " + sql);
    }
    bindParams(stmt, binds);
    int cols = sqlite3_column_count(stmt);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Row row;
        for (int c = 0; c < cols; ++c) {
            const char* name = sqlite3_column_name(stmt, c);
            std::string val;
            if (sqlite3_column_type(stmt, c) != SQLITE_NULL) {
                const unsigned char* t = sqlite3_column_text(stmt, c);
                if (t) val = reinterpret_cast<const char*>(t);
            }
            row[name] = val;
        }
        out.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    return out;
}

long long Database::lastInsertId() {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    return sqlite3_last_insert_rowid((sqlite3*)db_);
}

int Database::schemaVersion() {
    auto rows = query("PRAGMA user_version;");
    if (rows.empty()) return 0;
    // PRAGMA rows do not have named columns reliably; read positionally.
    for (const auto& kv : rows[0]) return kv.second.empty() ? 0 : std::stoi(kv.second);
    return 0;
}

void Database::migrate(const std::vector<std::string>& migrations) {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    int v = schemaVersion();
    for (size_t i = 0; i < migrations.size(); ++i) {
        int target = (int)(i + 1);
        if (target <= v) continue;
        exec(migrations[i]);
        exec("PRAGMA user_version=" + std::to_string(target) + ";");
        v = target;
        std::cout << "[db] applied migration " << target << "\n";
    }
}

void Database::begin() {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    if (inTx_) return; // nested: outer transaction owns the commit
    exec("BEGIN IMMEDIATE;");
    inTx_ = true;
}

void Database::commit() {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    if (!inTx_) return;
    exec("COMMIT;");
    inTx_ = false;
}

void Database::rollback() {
    std::lock_guard<std::recursive_mutex> lk(mtx_);
    if (!inTx_) return;
    exec("ROLLBACK;");
    inTx_ = false;
}

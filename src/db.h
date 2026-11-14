#ifndef DB_H
#define DB_H

//
// Transactional SQL storage for OpenVote.
//
// The environment has the SQLite runtime library but no PostgreSQL client
// development headers or an accessible PostgreSQL role, so persistence runs
// on SQLite (WAL mode) behind this small adapter. All service code goes
// through this interface only; a libpq-based backend can later implement
// the same interface without touching services.
//
// Thread safety: a single connection guarded by one recursive mutex. The
// HTTP server is multi-threaded (one thread per connection), so every
// database operation serializes here.
//

#include <string>
#include <vector>
#include <map>
#include <mutex>

// A bind value: either a text string or SQL NULL.
struct DbValue {
    std::string val;
    bool isNull = false;
    DbValue() : isNull(true) {}
    DbValue(const std::string& v) : val(v) {}
    DbValue(const char* v) : val(v) {}
    DbValue(long long n) : val(std::to_string(n)) {}
    static DbValue null() { return DbValue(); }
};

class Database {
public:
    explicit Database(const std::string& dbFile);
    ~Database();

    // Execute a statement (no result set). Throws std::runtime_error on failure.
    void exec(const std::string& sql);

    // Execute with bound parameters ('?' placeholders).
    void exec(const std::string& sql, const std::vector<DbValue>& binds);

    // Run a query with bound parameters. Values are returned as text
    // (SQLite text conversion); NULL becomes the empty string.
    using Row = std::map<std::string, std::string>;
    std::vector<Row> query(const std::string& sql,
                           const std::vector<DbValue>& binds = {});

    // Returns the rowid of the most recent successful INSERT.
    long long lastInsertId();

    // Schema migrations. Version tracked via PRAGMA user_version.
    // Each entry runs only when its (1-based) index equals the current
    // version + 1. Idempotent across restarts.
    void migrate(const std::vector<std::string>& migrations);

    // Transaction helpers (nested calls are ignored; use tx() where possible).
    void begin();      // BEGIN IMMEDIATE
    void commit();
    void rollback();

    // Run fn inside a BEGIN IMMEDIATE transaction; rolls back on throw.
    template <typename F>
    void tx(F&& fn) {
        std::lock_guard<std::recursive_mutex> lk(mtx_);
        bool outer = inTx_;
        begin();
        try {
            fn();
            if (!outer) commit();
        } catch (...) {
            if (!outer) rollback();
            throw;
        }
    }

    std::recursive_mutex& mutex() { return mtx_; }

    int schemaVersion();

private:
    void* db_ = nullptr;          // sqlite3*
    std::recursive_mutex mtx_;
    bool inTx_ = false;
};

#endif // DB_H

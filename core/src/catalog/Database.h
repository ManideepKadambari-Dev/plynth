#pragma once

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

struct sqlite3;
struct sqlite3_stmt;

namespace plynth::catalog {

// A database problem. Public catalog functions catch it and turn it into a DatabaseError status.
class DbError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// One SQLite connection, used from one thread. Private to core: no SQLite type appears in a public header.
class Database {
public:
    // Opens a database file with foreign keys on, WAL mode and a 5 second busy timeout. Throws DbError.
    static std::unique_ptr<Database> open(const std::filesystem::path& file, bool create);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Runs one or more statements that return nothing. Throws DbError.
    void exec(const std::string& sql);

    // PRAGMA user_version.
    int userVersion();

    // Transactions can be nested: only the outermost begin and commit reach SQLite.
    void begin();
    void commit();
    void rollback();

    sqlite3* handle() const { return db_; }

private:
    explicit Database(sqlite3* db) : db_(db) {}

    sqlite3* db_;
    int depth_ = 0;
    bool failed_ = false;
};

// Starts a transaction that takes the write lock at once, and rolls back unless commit is called.
class Transaction {
public:
    explicit Transaction(Database& db) : db_(db) { db_.begin(); }
    ~Transaction()
    {
        if (!done_) {
            try {
                db_.rollback();
            } catch (...) {
            }
        }
    }
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit()
    {
        done_ = true;
        db_.commit();
    }

private:
    Database& db_;
    bool done_ = false;
};

// A prepared statement. Parameters count from 1, columns from 0.
class Statement {
public:
    Statement(Database& db, std::string_view sql);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement& bind(int index, long long value);
    Statement& bind(int index, int value) { return bind(index, static_cast<long long>(value)); }
    Statement& bind(int index, double value);
    Statement& bind(int index, std::string_view value);
    Statement& bindNull(int index);

    // Starts over with the same statement: clears the parameters.
    void reset();

    // True when a row is ready, false when the statement is done. Throws DbError.
    bool step();
    // Runs a statement that returns no rows.
    void run();

    std::string text(int column) const;
    long long integer(int column) const;
    double real(int column) const;

private:
    Database& db_;
    sqlite3_stmt* stmt_ = nullptr;
};

}

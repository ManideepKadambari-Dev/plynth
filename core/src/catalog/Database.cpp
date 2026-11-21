#include "Database.h"

#include "plynth/core/ProjectFiles.h"

#include <sqlite3.h>

namespace plynth::catalog {

namespace {

// The message SQLite gives for the last error on a connection.
std::string lastMessage(sqlite3* db)
{
    return db ? sqlite3_errmsg(db) : "unknown error";
}

}

// Opens a database file.
std::unique_ptr<Database> Database::open(const std::filesystem::path& file, bool create)
{
    sqlite3* db = nullptr;
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_FULLMUTEX | (create ? SQLITE_OPEN_CREATE : 0);
    if (sqlite3_open_v2(pathToUtf8(file).c_str(), &db, flags, nullptr) != SQLITE_OK) {
        const std::string message = lastMessage(db);
        sqlite3_close(db);
        throw DbError("The database " + pathToUtf8(file) + " could not be opened: " + message);
    }
    std::unique_ptr<Database> result(new Database(db));
    sqlite3_busy_timeout(db, 5000);
    result->exec("PRAGMA foreign_keys = ON");
    result->exec("PRAGMA journal_mode = WAL");
    return result;
}

// Closes the connection.
Database::~Database()
{
    sqlite3_close(db_);
}

// Runs statements that return nothing.
void Database::exec(const std::string& sql)
{
    char* error = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : lastMessage(db_);
        sqlite3_free(error);
        throw DbError(message);
    }
}

// PRAGMA user_version.
int Database::userVersion()
{
    Statement s(*this, "PRAGMA user_version");
    return s.step() ? static_cast<int>(s.integer(0)) : 0;
}

// Starts a transaction, or joins the one running.
void Database::begin()
{
    if (depth_ == 0) {
        exec("BEGIN IMMEDIATE");
        failed_ = false;
    }
    ++depth_;
}

// Ends the outermost transaction with a commit, unless an inner one rolled back.
void Database::commit()
{
    if (depth_ == 0) return;
    if (--depth_ > 0) return;
    exec(failed_ ? "ROLLBACK" : "COMMIT");
}

// Marks the running transaction as failed and rolls back when the outermost one ends.
void Database::rollback()
{
    if (depth_ == 0) return;
    failed_ = true;
    if (--depth_ == 0) exec("ROLLBACK");
}

// Prepares a statement.
Statement::Statement(Database& db, std::string_view sql) : db_(db)
{
    if (sqlite3_prepare_v2(db.handle(), sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr) != SQLITE_OK) {
        throw DbError(lastMessage(db.handle()));
    }
}

// Frees the statement.
Statement::~Statement()
{
    sqlite3_finalize(stmt_);
}

// Binds a number.
Statement& Statement::bind(int index, long long value)
{
    sqlite3_bind_int64(stmt_, index, value);
    return *this;
}

// Binds a number.
Statement& Statement::bind(int index, double value)
{
    sqlite3_bind_double(stmt_, index, value);
    return *this;
}

// Binds text.
Statement& Statement::bind(int index, std::string_view value)
{
    sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    return *this;
}

// Binds a null.
Statement& Statement::bindNull(int index)
{
    sqlite3_bind_null(stmt_, index);
    return *this;
}

// Clears the statement for another run.
void Statement::reset()
{
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

// Moves to the next row.
bool Statement::step()
{
    const int code = sqlite3_step(stmt_);
    if (code == SQLITE_ROW) return true;
    if (code == SQLITE_DONE) return false;
    throw DbError(lastMessage(db_.handle()));
}

// Runs a statement to its end.
void Statement::run()
{
    while (step()) {
    }
}

// The text of a column.
std::string Statement::text(int column) const
{
    const unsigned char* p = sqlite3_column_text(stmt_, column);
    return p ? std::string(reinterpret_cast<const char*>(p), static_cast<std::size_t>(sqlite3_column_bytes(stmt_, column)))
             : std::string();
}

// The integer in a column.
long long Statement::integer(int column) const
{
    return sqlite3_column_int64(stmt_, column);
}

// The number in a column.
double Statement::real(int column) const
{
    return sqlite3_column_double(stmt_, column);
}

}

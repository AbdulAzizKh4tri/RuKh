/**
 * @file Sqlite3Types.hpp
 * @brief Types used for Sqlite3 implementation
 */
#pragma once

#include <sqlite3.h>
#include <unordered_map>

#include <rukh/core/AsyncPool.hpp>

namespace rukh::db {

/// RAII sqlite3_stmt guard
struct StatementResetGuard {
  sqlite3_stmt *&s;
  ~StatementResetGuard() {
    sqlite3_reset(s);
    sqlite3_clear_bindings(s);
  }
};

/// A Connection object with cached sqlite3_stmt statements.
struct Connection {
  sqlite3 *dbConnection;
  std::unordered_map<std::string, sqlite3_stmt *> statements;
};

} // namespace rukh::db

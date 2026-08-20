/**
 * @file IDatabase.hpp
 * @brief Database interface
 */
#pragma once

#include <expected>
#include <memory>
#include <string>
#include <sys/types.h>
#include <vector>

#include <nlohmann/json.hpp>

#include <rukh/core/Task.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/ITransaction.hpp>

namespace rukh::db {

/// Database interface that all database implementations must implement
class IDatabase {
public:
  /// Execute a query on the database with a vector of params.
  virtual core::Task<std::expected<QueryResult, DatabaseError>>
  executeQuery(const std::string &query, const std::vector<DbValue> &params = {}) = 0;

  /// Acquire a transaction, ownership transfers to caller. Must release.
  virtual std::expected<std::unique_ptr<ITransaction>, DatabaseError> acquireTransaction() = 0;

  /// Release a transaction back to the pool
  virtual void releaseTransaction(ITransaction *transaction) = 0;
};

} // namespace rukh::db

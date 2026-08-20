/**
 * @file ITransaction.hpp
 * @brief Transaction interface
 */
#pragma once

#include <expected>

#include <rukh/core/Task.hpp>
#include <rukh/db/DbTypes.hpp>

namespace rukh::db {

/// \todo Savepoints

/// Transaction interface that all database implementations must implement
class ITransaction {
public:
  virtual ~ITransaction() = default;

  /// Begin a transaction
  virtual core::Task<std::expected<QueryResult, DatabaseError>> begin(const std::string &mode = "DEFERRED") = 0;

  /// Commit a transaction
  virtual core::Task<std::expected<QueryResult, DatabaseError>> commit() = 0;

  /// Rollback a transaction
  virtual core::Task<std::expected<QueryResult, DatabaseError>> rollback() = 0;

  /// What to do when a transaction is abandoned. See @ref ScopedTransaction.
  virtual void abandon() = 0;

  /// Execute a parameterized query with params.
  virtual core::Task<std::expected<QueryResult, DatabaseError>>
  executeQuery(const std::string &query, const std::vector<DbValue> params = {}) = 0;

  /// Has the transaction been committed/rolledback.
  virtual bool isTransactionEnded() const = 0;
};

} // namespace rukh::db

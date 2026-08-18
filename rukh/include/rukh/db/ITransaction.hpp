#pragma once

#include <expected>

#include <rukh/core/Task.hpp>
#include <rukh/db/DbTypes.hpp>

namespace rukh::db {

// TODO: Savepoints
class ITransaction {
public:
  virtual ~ITransaction() = default;

  virtual core::Task<std::expected<QueryResult, DatabaseError>> begin(const std::string &mode = "DEFERRED") = 0;
  virtual core::Task<std::expected<QueryResult, DatabaseError>> commit() = 0;
  virtual core::Task<std::expected<QueryResult, DatabaseError>> rollback() = 0;
  virtual void abandon() = 0;

  virtual core::Task<std::expected<QueryResult, DatabaseError>>
  executeQuery(const std::string &query, const std::vector<DbValue> params = {}) = 0;

  virtual bool isTransactionEnded() const = 0;
};

} // namespace rukh::db

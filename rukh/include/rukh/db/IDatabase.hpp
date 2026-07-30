#pragma once

#include <expected>
#include <memory>
#include <string>
#include <sys/types.h>
#include <vector>

#include <nlohmann/json.hpp>

#include <rukh/Task.hpp>
#include <rukh/concepts.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/ITransaction.hpp>

namespace rukh::db {

class IDatabase {
public:
  virtual Task<std::expected<QueryResult, DatabaseError>> executeQuery(const std::string &query,
                                                                       const std::vector<DbValue> &params = {}) = 0;

  template <typename... Args>
  Task<std::expected<QueryResult, DatabaseError>> executeQuery(const std::string &sql, Args &&...args) {
    return executeQuery(sql, std::vector<DbValue>{std::forward<Args>(args)...});
  }

  virtual std::expected<std::unique_ptr<ITransaction>, DatabaseError> acquireTransaction() = 0;
  virtual void releaseTransaction(ITransaction *transaction) = 0;
};

} // namespace rukh::db

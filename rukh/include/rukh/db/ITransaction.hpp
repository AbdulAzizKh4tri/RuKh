#pragma once

#include <expected>

#include <rukh/db/DbTypes.hpp>

namespace rukh::db {

class ITransaction {
public:
  virtual ~ITransaction() = default;

  virtual bool begin(const std::string &mode) = 0;
  virtual bool commit() = 0;
  virtual bool rollback() = 0;

  virtual std::expected<QueryResult, DatabaseError> executeQuery(const std::string &query,
                                                                 const std::vector<DbValue> params = {}) = 0;

  virtual bool isTransactionEnded() const = 0;
};

} // namespace rukh::db

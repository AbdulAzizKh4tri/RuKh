#pragma once

#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>

namespace rukh::db {

inline std::expected<QueryResult, DatabaseError> dispatch(IDatabase *db, ITransaction *transaction,
                                                          const std::string &sql, const std::vector<DbValue> &params) {
  if (transaction)
    return transaction->executeQuery(sql, params);
  return db->executeQuery(sql, params);
}

} // namespace rukh::db

#pragma once

#include <rukh/Task.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/orm/OrmConfig.hpp>
#include <rukh/orm/hydrators.hpp>

namespace rukh::orm {

class QueryBase {
  inline static db::IDatabase *db_ = OrmConfig::db;

  static Task<std::expected<db::QueryResult, db::DatabaseError>>
  dispatch(db::ITransaction *transaction, const std::string &sql, const std::vector<db::DbValue> &params) {
    if (transaction)
      return transaction->executeQuery(sql, params);
    return db_->executeQuery(sql, params);
  }
};

} // namespace rukh::orm

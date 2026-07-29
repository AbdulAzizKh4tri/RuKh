#pragma once

#include <rukh/Task.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/helpers.hpp>
#include <rukh/orm/hydrators.hpp>

namespace rukh::orm {

template <typename Model, typename Derived> class QueryBase {
protected:
  db::IDatabase *db_ = Model::db;

  Task<std::expected<db::QueryResult, db::DatabaseError>>
  dispatch(db::ITransaction *transaction, const std::string &sql, const std::vector<db::DbValue> &params) {
    co_return co_await Model::threadPool->submit([&, this]() -> std::expected<db::QueryResult, db::DatabaseError> {
      return db::dispatch(db_, transaction, sql, params);
    });
  }
};

} // namespace rukh::orm

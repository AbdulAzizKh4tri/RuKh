#pragma once

#include <rukh/Task.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/orm/OrmConfig.hpp>
#include <rukh/orm/hydrators.hpp>

namespace rukh::orm {

template <typename Derived, typename... Models> class QueryBase {
public:
  using ModelsTuple = std::tuple<Models...>;
  using Model = std::tuple_element_t<0, ModelsTuple>;

protected:
  db::IDatabase *db_ = OrmConfig::db;

  Task<std::expected<db::QueryResult, db::DatabaseError>>
  dispatch(db::ITransaction *transaction, const std::string &sql, const std::vector<db::DbValue> &params) {
    if (transaction)
      co_return co_await transaction->executeQuery(sql, params);
    co_return co_await db_->executeQuery(sql, params);
  }
};

} // namespace rukh::orm

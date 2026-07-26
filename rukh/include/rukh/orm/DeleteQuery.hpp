#pragma once

#include <cstddef>
#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/Task.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/orm/Hydrator.hpp>
#include <rukh/orm/Predicate.hpp>
#include <rukh/orm/WhereClause.hpp>

namespace rukh::orm {

template <typename Model> class DeleteQuery : public WhereClause<DeleteQuery<Model>> {
public:
  Task<std::pair<size_t, std::vector<Model>>> execute(rukh::db::IDatabase *db, bool returning = false) {

    buildDeleteSqlAndSetParams(returning);
    auto queryResult = co_await Model::threadPool->submit(
        [db, this]() -> std::expected<db::QueryResult, db::DatabaseError> { return db->executeQuery(sql_, params_); });

    if (not queryResult) {
      SPDLOG_ERROR("Error executing query: {}", sql_);
      SPDLOG_ERROR("DatabaseError: {}", queryResult.error().message);
      co_return {0, {}};
    }

    co_return std::make_pair(queryResult->affectedRows, hydrate<Model>(*queryResult));
  }

private:
  std::string sql_;
  std::vector<rukh::db::DbValue> params_;

  void buildDeleteSqlAndSetParams(bool returning = false) {
    sql_ = sqlInit;
    params_.clear();

    if (this->wherePredicate) {
      sql_ += " WHERE ";
      sql_ += Predicate::resolvePredicates(*this->wherePredicate, params_);
    } else {
      sql_ = "";
      SPDLOG_WARN("No where clause when deleting: {}. use where({{1, = ,1}}) if you want to delete all",
                  Model::tableName);
    }

    if (returning)
      sql_ += " RETURNING *";

    sql_ += ';';
  }

  static inline std::string sqlInit = "DELETE FROM " + Model::tableName + " ";
};

} // namespace rukh::orm

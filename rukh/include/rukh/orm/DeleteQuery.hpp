#pragma once

#include <cstddef>
#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/Task.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/orm/Predicate.hpp>
#include <rukh/orm/WhereClause.hpp>
#include <rukh/orm/hydrators.hpp>

namespace rukh::orm {

template <typename Model> class DeleteQuery : public WhereClause<Model, DeleteQuery<Model>> {
public:
  Task<std::pair<size_t, std::vector<Model>>> execute(bool returning = false) {

    buildDeleteSqlAndSetParams(returning);
    auto queryResult = co_await Model::threadPool->submit(
        [this]() -> std::expected<db::QueryResult, db::DatabaseError> { return db_->executeQuery(sql_, params_); });

    if (not queryResult) {
      SPDLOG_ERROR("Error executing query: {}", sql_);
      SPDLOG_ERROR("DatabaseError: {}", queryResult.error().message);
      co_return {0, {}};
    }

    co_return std::make_pair(queryResult->affectedRows, hydrate<Model>(*queryResult));
  }

  DeleteQuery<Model> &reset() {
    this->whereChanged = true;
    this->wherePredicate = std::nullopt;
    params_.clear();
    return *this;
  }

private:
  db::IDatabase *db_ = Model::db;
  std::string sql_;
  std::vector<rukh::db::DbValue> params_;

  void buildDeleteSqlAndSetParams(bool returning = false) {
    sql_ = sqlInit;
    params_.clear();

    if (this->wherePredicate) {
      sql_ += " WHERE ";
      sql_ += Predicate<Model>::resolvePredicates(*this->wherePredicate, params_);
    } else {
      throw rukh::OrmException("No where clause when deleting: " + Model::tableName +
                               ". use where({{true}}) if you want to delete all");
    }

    if (returning)
      sql_ += " RETURNING *";

    sql_ += ';';
  }

  static inline std::string sqlInit = "DELETE FROM " + Model::tableName + " ";
};

} // namespace rukh::orm

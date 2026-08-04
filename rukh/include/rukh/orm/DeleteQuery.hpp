#pragma once

#include <cstddef>
#include <expected>
#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/Task.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>
#include <rukh/orm/Predicate.hpp>
#include <rukh/orm/QueryBase.hpp>
#include <rukh/orm/WhereClause.hpp>
#include <rukh/orm/hydrators.hpp>

namespace rukh::orm {

template <typename Model>
class DeleteQuery : public WhereClause<DeleteQuery<Model>, Model>, public QueryBase<DeleteQuery<Model>, Model> {
public:
  Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  execute(db::ITransaction *transaction = nullptr, bool returning = false) {

    buildDeleteSqlAndSetParams(returning);
    auto queryResult = co_await this->dispatch(transaction, sql_, params_);

    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return std::make_pair(queryResult->affectedRows, hydrate<Model>(*queryResult));
  }

  DeleteQuery<Model> &reset() {
    this->wherePredicate = std::nullopt;
    params_.clear();
    return *this;
  }

private:
  std::string sql_;
  std::vector<rukh::db::DbValue> params_;

  void buildDeleteSqlAndSetParams(bool returning = false) {
    sql_ = sqlInit;
    params_.clear();

    if (this->wherePredicate) {
      sql_ += " WHERE ";
      sql_ += (*this->wherePredicate).resolvePredicates(params_);
    } else {
      throw rukh::OrmException("No where clause when deleting: " + std::string(Model::tableName) +
                               ". use where({true}) if you want to delete all");
    }

    if (returning)
      sql_ += " RETURNING *";

    sql_ += ';';
  }

  inline static std::string sqlInit = "DELETE FROM " + std::string(Model::tableName) + " AS " + getAlias(0);
};

} // namespace rukh::orm

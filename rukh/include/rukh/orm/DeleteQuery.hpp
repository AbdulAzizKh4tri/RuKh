#pragma once

#include <cstddef>
#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/orm/Hydrator.hpp>
#include <rukh/orm/Predicate.hpp>

namespace rukh::orm {

template <typename Model> class DeleteQuery {
public:
  std::pair<size_t, std::vector<Model>> execute(rukh::db::IDatabase *db, bool returning = false) {

    buildDeleteSqlAndSetParams();
    auto queryResult = db->executeQuery(sql_, params_);

    if (not queryResult) {
      SPDLOG_ERROR("Error executing query: {}", sql_);
      SPDLOG_ERROR("DatabaseError: {}", queryResult.error().message);
      return {0, {}};
    }

    return std::make_pair(queryResult->affectedRows, hydrate<Model>(*queryResult));
  }

  DeleteQuery &where(Predicate p) {
    if (not wherePredicate_.has_value())
      wherePredicate_ = p;
    else
      wherePredicate_ = *wherePredicate_ && p;
    return *this;
  }

  DeleteQuery &andWhere(Predicate p) { return where(p); }

  DeleteQuery &orWhere(Predicate p) {
    if (not wherePredicate_.has_value())
      wherePredicate_ = p;
    else
      wherePredicate_ = *wherePredicate_ || p;
    return *this;
  }

private:
  std::string sql_;
  std::vector<rukh::db::DbValue> params_;
  std::optional<Predicate> wherePredicate_ = std::nullopt;

  void buildDeleteSqlAndSetParams(bool returning = false) {
    sql_ = sqlInit;
    params_.clear();

    if (wherePredicate_) {
      sql_ += " WHERE ";
      sql_ += Predicate::resolvePredicates(*wherePredicate_, params_);
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

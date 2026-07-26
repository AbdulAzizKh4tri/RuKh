#pragma once

#include <cstddef>
#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/orm/Hydrator.hpp>
#include <rukh/orm/Predicate.hpp>

namespace rukh::orm {

template <typename Model> class UpdateQuery {
public:
  std::pair<size_t, std::vector<Model>> execute(rukh::db::IDatabase *db, const Model &obj, bool returning = false) {
    buildUpdateSqlAndSetParams(obj, returning);
    auto queryResult = db->executeQuery(sql_, params_);

    if (not queryResult) {
      SPDLOG_ERROR("Error executing query: {}", sql_);
      SPDLOG_ERROR("DatabaseError: {}", queryResult.error().message);
      return std::make_pair(0, std::vector<Model>{obj});
    }

    return std::make_pair(queryResult->affectedRows, hydrate<Model>(*queryResult));
  }

  UpdateQuery &column(std::string col) {
    columns_.push_back(col);
    return *this;
  }

  UpdateQuery &where(Predicate p) {
    if (not wherePredicate_.has_value())
      wherePredicate_ = p;
    else
      wherePredicate_ = *wherePredicate_ && p;
    return *this;
  }

  UpdateQuery &andWhere(Predicate p) { return where(p); }

  UpdateQuery &orWhere(Predicate p) {
    if (not wherePredicate_.has_value())
      wherePredicate_ = p;
    else
      wherePredicate_ = *wherePredicate_ || p;
    return *this;
  }

private:
  std::string sql_;
  std::vector<std::string> columns_;
  std::vector<rukh::db::DbValue> params_;
  std::optional<Predicate> wherePredicate_ = std::nullopt;

  void buildUpdateSqlAndSetParams(const Model &obj, bool returning = false) {
    sql_ = sqlInit;
    params_.clear();

    sql_ += modelColumnValueUpdateListString(obj);

    if (wherePredicate_) {
      sql_ += " WHERE ";
      sql_ += Predicate::resolvePredicates(*wherePredicate_, params_);
    }

    if (returning)
      sql_ += " RETURNING *";

    sql_ += ';';
  }

  std::string modelColumnValueUpdateListString(const Model &obj) {
    std::ostringstream oss;
    bool first = true;
    std::apply(
        [&](auto &&...col) {
          auto handle = [&](auto &&c) {
            bool isSkippedPk = Model::pkAutoIncrement && c.name == Model::pkColumn().name;
            bool notSelected =
                not columns_.empty() && std::find(columns_.begin(), columns_.end(), c.name) == columns_.end();
            if (isSkippedPk || notSelected)
              return;

            if (not first)
              oss << ", ";
            first = false;
            oss << c.name << " = ?";
            params_.push_back(obj.*c.fieldPtr);
          };
          (handle(col), ...);
        },
        Model::columns());
    oss << " ";
    return oss.str();
  }

  static inline std::string sqlInit = "Update " + Model::tableName + " SET ";
};

} // namespace rukh::orm

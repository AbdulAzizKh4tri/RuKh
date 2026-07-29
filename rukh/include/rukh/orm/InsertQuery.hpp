#pragma once

#include <cstddef>
#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/Task.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>
#include <rukh/db/helpers.hpp>
#include <rukh/orm/Predicate.hpp>
#include <rukh/orm/hydrators.hpp>

namespace rukh::orm {

template <typename Model> class InsertQuery {
public:
  Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  execute(const std::vector<Model> &objs, db::ITransaction *transaction = nullptr, bool returning = false) {
    if (objs.empty())
      co_return std::make_pair(0, std::vector<Model>());

    buildInsertSqlAndSetParams(objs, returning);

    auto queryResult =
        co_await Model::threadPool->submit([this, transaction]() -> std::expected<db::QueryResult, db::DatabaseError> {
          return db::dispatch(db_, transaction, sql_, params_);
        });

    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return std::make_pair(queryResult->affectedRows, hydrate<Model>(*queryResult));
  }

private:
  db::IDatabase *db_ = Model::db;
  std::string sql_;
  std::vector<rukh::db::DbValue> params_;

  void buildInsertSqlAndSetParams(const std::vector<Model> &objs, bool returning = false) {
    params_.clear();
    sql_ = sqlInit;

    sql_ += "VALUES ";
    sql_ += valuesListStringAndParams(objs);

    if (returning)
      sql_ += " RETURNING *";
    sql_ += ';';
  }

  std::string valuesListStringAndParams(const std::vector<Model> &objs) {
    std::ostringstream oss;
    bool firstRow = true;
    for (const auto &obj : objs) {
      if (not firstRow)
        oss << ", ";
      firstRow = false;

      oss << "(";
      bool first = true;
      std::apply(
          [&](auto &&...col) {
            auto handle = [&](auto &&c) {
              if (Model::pkAutoIncrement and Model::isPkColumn(c.name))
                return;
              db::DbValue columnValue = db::toDbValue(obj.*c.fieldPtr);
              if (not first)
                oss << ", ";
              first = false;
              oss << "?";
              params_.push_back(columnValue);
            };
            (handle(col), ...);
          },
          Model::columns());
      oss << ")";
    }
    return oss.str();
  }

  static inline const std::string &modelColumnListString() {
    static const std::string cached = [] {
      std::string str;
      bool first = true;
      std::apply(
          [&](auto &&...col) {
            auto handle = [&](auto &&c) {
              if (Model::pkAutoIncrement and Model::isPkColumn(c.name))
                return;
              str += (first ? (first = false, "") : ", ") + c.name;
            };
            (handle(col), ...);
          },
          Model::columns());
      return str;
    }();
    return cached;
  }
  static inline std::string sqlInit = "INSERT INTO " + Model::tableName + " (" + modelColumnListString() + ") ";
};

} // namespace rukh::orm

#pragma once

#include <cstddef>
#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/orm/Hydrator.hpp>
#include <rukh/orm/Predicate.hpp>

namespace rukh::orm {

template <typename Model> class InsertQuery {
public:
  std::pair<size_t, std::vector<Model>> execute(rukh::db::IDatabase *db, const std::vector<Model> &objs,
                                                bool returning = false) {
    buildInsertSqlAndSetParams(objs, returning);
    auto queryResult = db->executeQuery(sql_, params_);

    if (not queryResult) {
      SPDLOG_ERROR("Error executing query: {}", sql_);
      SPDLOG_ERROR("DatabaseError: {}", queryResult.error().message);
      return std::make_pair(0, std::vector<Model>());
    }

    return std::make_pair(queryResult->affectedRows, hydrate<Model>(*queryResult));
  }

private:
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
              if (Model::pkAutoIncrement && c.name == Model::pkColumn().name)
                return;
              if (not first)
                oss << ", ";
              first = false;
              oss << "?";
              params_.push_back(obj.*c.fieldPtr);
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
              if (Model::pkAutoIncrement && c.name == Model::pkColumn().name)
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

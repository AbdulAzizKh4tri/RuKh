#pragma once

#include <cstddef>
#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/Task.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>
#include <rukh/orm/Column.hpp>
#include <rukh/orm/Predicate.hpp>
#include <rukh/orm/QueryBase.hpp>
#include <rukh/orm/hydrators.hpp>
#include <sstream>

namespace rukh::orm {

template <typename Model> class InsertQuery : public QueryBase<InsertQuery<Model>> {
public:
  Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  execute(const std::vector<Model> &objs, db::ITransaction *transaction = nullptr, bool returning = false) {
    if (objs.empty())
      co_return std::make_pair(0, std::vector<Model>());

    buildInsertSqlAndSetParams(objs, returning);

    auto queryResult = co_await this->dispatch(transaction, sql_, params_);

    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return std::make_pair(queryResult->affectedRows, hydrate<Model>(*queryResult));
  }

private:
  std::string sql_;
  std::vector<rukh::db::DbValue> params_;

  void buildInsertSqlAndSetParams(const std::vector<Model> &objs, bool returning = false) {
    params_.clear();
    std::ostringstream ss;
    ss << sqlInit << " VALUES " << valuesListStringAndParams(objs);

    if (returning)
      ss << " RETURNING *";
    ss << ';';
    sql_ = ss.str();
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
              if (columnShouldBeSkipped(c.autoGenerateMode))
                return;

              db::DbValue columnValue;

              switch (c.autoGenerateMode) {
              case AutoGenerate::OFF:
                columnValue = db::toDbValue(obj.*c.fieldPtr);
                break;
              case AutoGenerate::CUSTOM:
                columnValue = (obj.*c.customGenerator)();
                break;
              default:
                throw OrmException("Unknown AutoGeneration type");
              }

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
      std::ostringstream oss;
      bool first = true;
      std::apply(
          [&](auto &&...col) {
            auto handle = [&](auto &&c) {
              if (columnShouldBeSkipped(c.autoGenerateMode))
                return;

              oss << (first ? (first = false, "") : ", ") << c.dbName;
            };
            (handle(col), ...);
          },
          Model::columns());
      return oss.str();
    }();
    return cached;
  }

  static inline const bool columnShouldBeSkipped(AutoGenerate policy) {
    return policy == AutoGenerate::DB_INCREMENT or policy == AutoGenerate::DEFAULT or policy == AutoGenerate::DB_NOW;
  }

  inline static std::string sqlInit =
      "INSERT INTO " + std::string(Model::tableName) + " (" + modelColumnListString() + ") ";
};

} // namespace rukh::orm

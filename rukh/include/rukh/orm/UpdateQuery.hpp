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
#include <rukh/orm/WhereClause.hpp>
#include <rukh/orm/hydrators.hpp>

namespace rukh::orm {

template <typename Model>
class UpdateQuery : public WhereClause<Model, UpdateQuery<Model>>, public QueryBase<Model, UpdateQuery<Model>> {
public:
  Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  execute(const Model &obj, db::ITransaction *transaction = nullptr, bool returning = false) {

    buildUpdateSqlAndSetParams(obj, returning);

    auto queryResult = co_await this->dispatch(transaction, sql_, params_);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return std::make_pair(queryResult->affectedRows, hydrate<Model>(*queryResult));
  }

  template <typename FieldPtr> UpdateQuery &field(FieldPtr fieldPtr) {
    columns_.push_back(Model::columnNameOf(fieldPtr));
    return *this;
  }

  UpdateQuery &field(std::string column) {
    if (not Model::isValidColumnName(column))
      throw rukh::OrmException("Failed to add column to query: unknown column '" + column + "' on " + Model::tableName);
    columns_.push_back(column);
    return *this;
  }

  UpdateQuery<Model> &reset() {
    this->whereChanged = true;
    this->wherePredicate = std::nullopt;
    columns_.clear();
    this->params_.clear();
    return *this;
  }

private:
  std::string sql_;
  std::vector<std::string> columns_;
  std::vector<db::DbValue> params_;

  void buildUpdateSqlAndSetParams(const Model &obj, bool returning = false) {
    this->sql_ = sqlInit;
    this->params_.clear();

    this->sql_ += modelUpdateValueList(obj);

    if (this->wherePredicate) {
      this->sql_ += " WHERE ";
      this->sql_ += Predicate<Model>::resolvePredicates(*this->wherePredicate, this->params_);
    } else {
      throw rukh::OrmException("No where clause when Updating: " + Model::tableName +
                               ". use where({{true}}) if you want to update all");
    }

    if (returning)
      this->sql_ += " RETURNING *";

    this->sql_ += ';';
  }

  std::string modelUpdateValueList(const Model &obj) {
    std::ostringstream oss;
    bool first = true;
    std::apply(
        [&](auto &&...col) {
          auto handle = [&](auto &&c) {
            bool notSelected =
                not columns_.empty() and std::find(columns_.begin(), columns_.end(), c.dbName) == columns_.end();
            if (c.isPrimaryKey or notSelected or columnShouldBeSkipped(c.autoUpdateMode))
              return;

            db::DbValue columnValue;

            switch (c.autoUpdateMode) {
            case AutoUpdate::OFF:
              columnValue = db::toDbValue(obj.*c.fieldPtr);
              break;
            case AutoUpdate::CUSTOM:
              columnValue = (obj.*c.customUpdator)();
              break;
            default:
              throw OrmException("Unknown Auto Update type");
            }

            if (not first)
              oss << ", ";
            first = false;
            oss << c.dbName << " = ?";
            params_.push_back(columnValue);
          };
          (handle(col), ...);
        },
        Model::columns());
    oss << " ";
    return oss.str();
  }

  static inline const bool columnShouldBeSkipped(AutoUpdate policy) { return policy == AutoUpdate::DB_SIDE; }

  static inline std::string sqlInit = "UPDATE " + Model::tableName + " SET ";
};

} // namespace rukh::orm

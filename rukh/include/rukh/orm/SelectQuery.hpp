#pragma once

#include "rukh/db/DbTypes.hpp"
#include <optional>
#include <spdlog/spdlog.h>
#include <sstream>

#include <rukh/Exceptions.hpp>
#include <rukh/Task.hpp>
#include <rukh/TypeHelpers.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>
#include <rukh/orm/DeleteQuery.hpp>
#include <rukh/orm/InsertQuery.hpp>
#include <rukh/orm/Predicate.hpp>
#include <rukh/orm/QueryBase.hpp>
#include <rukh/orm/UpdateQuery.hpp>
#include <rukh/orm/WhereClause.hpp>
#include <rukh/orm/hydrators.hpp>
#include <string>

namespace rukh::orm {

enum class Sorting { DESC, ASC };
enum class JoinType { INNER, LEFT, RIGHT, FULL, CROSS };

template <typename... Models>
class SelectQuery : public QueryBase<SelectQuery<Models...>, Models...>,
                    public WhereClause<SelectQuery<Models...>, Models...> {
  /*
   * Every Query has a Main Model, the first one defined in the template params.
   * This is the only one that can use methods like update, getOne, etc.
   * If you are not using JOINS, you need not worry about this.
   */

  static constexpr std::string_view joinTypeStrings[] = {" JOIN ", " LEFT JOIN ", " RIGHT JOIN ", " FULL JOIN ",
                                                         " CROSS JOIN "};
  static constexpr std::string_view to_string(JoinType joinType) {
    return joinTypeStrings[static_cast<std::size_t>(joinType)];
  }

  using ModelsTuple = std::tuple<Models...>;
  using Model = std::tuple_element_t<0, ModelsTuple>;
  static constexpr std::size_t modelCount = sizeof...(Models);

public:
  // TODO: ADD STRING ESCAPE HATCHES FOR ALL THESE;

  SelectQuery() {}
  SelectQuery(const std::string &tableAlias) : tableAlias_(tableAlias) {}

  template <typename JoinModel>
  SelectQuery &join(const Predicate<Models...> &predicate, const std::optional<std::string> tableAlias = std::nullopt,
                    JoinType joinType = JoinType::INNER) {
    static_assert(is_in_tuple_v<JoinModel, ModelsTuple>, "JoinModel not found in provided SelectQuery Model types");
    if (not joins_)
      joins_ = std::vector<Join>();
    joins_->emplace_back(JoinModel::tableName, (tableAlias.value_or(getAlias(get_index_of_v<JoinModel, ModelsTuple>))),
                         to_string(joinType), predicate);
    return *this;
  }

  template <typename FieldPtr>
  SelectQuery &field(FieldPtr fieldPtr, const std::string &tableAlias = "", const std::string &columnAlias = "") {
    columns_.push_back(getColumnWithAliases(fieldPtr, tableAlias, columnAlias));
    return *this;
  }

  template <typename FieldPtr>
  SelectQuery &orderBy(FieldPtr fieldPtr, Sorting sorting = Sorting::ASC, const std::string &tableAlias = "") {
    orderBy_.emplace_back(getColumnWithAliases(fieldPtr, tableAlias), sorting);
    return *this;
  }

  SelectQuery &limit(size_t limit) {
    if (limit == 0)
      throw rukh::OrmException("Limit must be greater than 0");
    limit_ = limit;
    return *this;
  }

  SelectQuery &offset(size_t offset) {
    offset_ = offset_.value_or(0) + offset;
    return *this;
  }

  template <typename FieldPtr> SelectQuery &groupBy(FieldPtr fieldPtr, const std::string &tableAlias = "") {
    groupBy_.push_back(getColumnWithAliases(fieldPtr, tableAlias));
    return *this;
  }

  // Count the number of rows
  Task<std::expected<int64_t, db::DatabaseError>> count(db::ITransaction *transaction = nullptr,
                                                        bool distinct = false) {
    std::ostringstream countSs;
    std::vector<db::DbValue> countParams;

    countSs << "SELECT " << (distinct ? "DISTINCT COUNT(*)" : "COUNT(*)") << " FROM " << std::string(Model::tableName)
            << " AS " << tableAlias_.value_or(getAlias(get_index_of_v<Model, ModelsTuple>));

    if (joins_) {
      for (auto &join : *joins_) {
        countSs << join.type << join.tableName << " AS " << join.tableAlias << " ON "
                << join.condition.resolvePredicates(params_);
      }
    }

    if (this->wherePredicate) {
      countSs << " WHERE " << (*this->wherePredicate).resolvePredicates(countParams);
    }

    if (!groupBy_.empty()) {
      countSs << " GROUP BY " << groupBy_.at(0);
      for (size_t i = 1; i < groupBy_.size(); i++) {
        countSs << ", " << groupBy_.at(i);
      }
    }

    auto queryResult = co_await this->dispatch(transaction, countSs.str(), countParams);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return queryResult->rows[0].template as<int64_t>(0).value_or(0);
  }

  // Count the non-null values of a field
  template <typename FieldPtr>
  Task<std::expected<int64_t, db::DatabaseError>> count(FieldPtr fieldPtr, db::ITransaction *transaction = nullptr,
                                                        bool distinct = false, const std::string &tableAlias = "") {
    std::ostringstream countSs;
    std::vector<db::DbValue> countParams;
    auto columnWithAlias = getColumnWithAliases(fieldPtr, tableAlias);

    countSs << "SELECT " << (distinct ? "DISTINCT COUNT(" + columnWithAlias + ")" : "COUNT(" + columnWithAlias + ")")
            << " FROM " << std::string(Model::tableName) << " AS "
            << tableAlias_.value_or(getAlias(get_index_of_v<Model, ModelsTuple>));

    if (joins_) {
      for (auto &join : *joins_) {
        countSs << join.type << join.tableName << " AS " << join.tableAlias << " ON "
                << join.condition.resolvePredicates(params_);
      }
    }

    if (this->wherePredicate) {
      countSs << " WHERE " << (*this->wherePredicate).resolvePredicates(countParams);
    }

    if (!groupBy_.empty()) {
      countSs << " GROUP BY " << groupBy_.at(0);
      for (size_t i = 1; i < groupBy_.size(); i++) {
        countSs << ", " << groupBy_.at(i);
      }
    }

    auto queryResult = co_await this->dispatch(transaction, countSs.str(), countParams);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return queryResult->rows[0].template as<int64_t>(0).value_or(0);
  }

  /*
   * Returns a single object, throws if 0 or multiple rows are returned.
   * Cannot be used with JOIN Queries that return columns from more than one table;.
   */
  Task<std::expected<Model, db::DatabaseError>> getOne(db::ITransaction *transaction = nullptr) {
    buildSelectSqlAndSetParams(2);

    auto queryResult = co_await this->dispatch(transaction, this->sql_, this->params_);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    auto rowCount = queryResult->rows.size();

    if (rowCount > 1)
      throw rukh::OrmException("getOne(): Got more than one row, expected only one");
    if (rowCount < 1)
      throw rukh::OrmException("getOne(): Found no matching rows");

    co_return hydrate<Model>(queryResult->rows[0]);
  }

  /*
   * Returns a single object, throws if multiple rows are returned.
   * Cannot be used with JOIN Queries that return columns from more than one table;.
   */
  Task<std::expected<std::optional<Model>, db::DatabaseError>> getOneOptional(db::ITransaction *transaction) {
    buildSelectSqlAndSetParams(2);
    auto queryResult = co_await this->dispatch(transaction, sql_, params_);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    auto rowCount = queryResult->rows.size();
    if (rowCount > 1)
      throw rukh::OrmException("get(): Got more than one row when expected only one");
    if (rowCount == 0)
      co_return std::nullopt;
    co_return hydrate<Model>(queryResult->rows[0]);
  };

  /*
   * Cannot be used with JOIN Queries that return columns from more than one table;.
   */
  Task<std::expected<std::vector<Model>, db::DatabaseError>> select(db::ITransaction *transaction = nullptr) {
    buildSelectSqlAndSetParams();
    auto queryResult = co_await this->dispatch(transaction, this->sql_, this->params_);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return hydrate<Model>(*queryResult);
  }

  /*
   * Cannot be used with JOIN Queries that return columns from more than one table;.
   */
  Task<std::expected<std::optional<Model>, db::DatabaseError>> first(db::ITransaction *transaction = nullptr) {
    buildSelectSqlAndSetParams(1);

    auto queryResult = co_await this->dispatch(transaction, this->sql_, this->params_);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    if (queryResult->rows.empty())
      co_return std::nullopt;

    co_return hydrate<Model>(queryResult->rows[0]);
  }

  /*
   * Cannot be used with JOIN Queries that return columns from more than one table;.
   */
  Task<std::expected<bool, db::DatabaseError>> exists(db::ITransaction *transaction = nullptr) {
    buildSelectSqlAndSetParams(1);

    auto queryResult = co_await this->dispatch(transaction, this->sql_, this->params_);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return queryResult->rows.size() > 0;
  }

  /*
   * FOOTGUN ALERT!!
   * It is the callers responsibility to ensure that the row described by the where clause matches the values of the
   * objToCreate. Otherwise a situation may arise where the where clause returns nothing for a query, and creates a
   * completely different object based on objToCreate, which may already exist.
   *
   * Cannot be used with JOIN Queries that return columns from more than one table;.
   */
  Task<std::expected<Model, db::DatabaseError>> getOneOrCreate(const Model &objToCreate,
                                                               db::ITransaction *transaction = nullptr) {
    if (not this->wherePredicate)
      throw rukh::OrmException("getOneOrCreate(): no where predicate set");

    auto getResult = co_await getOneOptional(transaction);
    if (not getResult)
      co_return std::unexpected(getResult.error());

    auto userOpt = *getResult;
    if (userOpt)
      co_return *userOpt;

    auto insertResult = co_await InsertQuery<Model>().execute({objToCreate}, transaction, true);
    if (not insertResult) {
      if (insertResult.error().type == db::DbErrorType::DUPLICATE_KEY or
          insertResult.error().type == db::DbErrorType::UNIQUE_CONSTRAINT_VIOLATION) {
        co_return co_await getOne(transaction);
      } else {
        co_return std::unexpected(insertResult.error());
      }
    }
    auto [_, objs] = *insertResult;
    co_return objs[0];
  }

  Task<std::expected<db::QueryResult, db::DatabaseError>> execute(db::ITransaction *transaction = nullptr) {
    buildSelectSqlAndSetParams();
    auto queryResult = co_await this->dispatch(transaction, this->sql_, this->params_);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return queryResult;
  }

  // Cannot be used with JOIN Queries that return columns from more than one table;.
  Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  update(const Model &obj, db::ITransaction *transaction = nullptr, bool returning = false) {
    UpdateQuery<Model> updateQuery;
    for (auto &col : columns_) {
      updateQuery.field(col);
    }
    return updateQuery.where(this->wherePredicate.value_or(Predicate<Model>::truePredicate()))
        .execute(obj, transaction, returning);
  }

  // Cannot be used with JOIN Queries that return columns from more than one table;.
  Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  destroy(db::ITransaction *transaction = nullptr, bool returning = false) {
    return DeleteQuery<Model>()
        .where(this->wherePredicate.value_or(Predicate<Model>::truePredicate()))
        .execute(transaction, returning);
  }

  std::string getSql() {
    buildSelectSqlAndSetParams();
    return this->sql_;
  }

  SelectQuery<Models...> &reset() {
    this->wherePredicate = std::nullopt;
    columns_.clear();
    this->params_.clear();
    orderBy_.clear();
    groupBy_.clear();
    limit_ = std::nullopt;
    offset_ = std::nullopt;
    return *this;
  }

private:
  struct Join {
    std::string_view tableName;
    std::string tableAlias;
    std::string_view type;
    Predicate<Models...> condition;
  };

  std::string sql_;
  std::optional<std::string> tableAlias_ = std::nullopt;
  std::optional<std::vector<Join>> joins_ = std::nullopt;
  std::vector<std::string> columns_;
  std::vector<db::DbValue> params_;
  std::vector<std::pair<std::string, Sorting>> orderBy_;
  std::vector<std::string> groupBy_;
  std::optional<size_t> limit_;
  std::optional<size_t> offset_;

  template <typename FieldPtr>
  std::string getColumnWithAliases(FieldPtr fieldPtr, const std::string &tableAlias, const std::string &columnAlias) {
    using FieldPtrModel = get_class_t<FieldPtr>;
    std::string asColumnAlias = columnAlias.empty() ? "" : " AS " + columnAlias;
    if (tableAlias.empty())
      return getAlias(get_index_of_v<FieldPtrModel, ModelsTuple>) + "." + FieldPtrModel::columnNameOf(fieldPtr) +
             asColumnAlias;
    return tableAlias + "." + FieldPtrModel::columnNameOf(fieldPtr) + asColumnAlias;
  }

  void buildSelectSqlAndSetParams(std::optional<size_t> limit = std::nullopt) {
    params_.clear();

    std::ostringstream ss;
    ss << "SELECT ";

    if (columns_.empty()) {
      constexpr auto mainModelColumns = Model::columns();
      bool first = true;
      std::apply(
          [&](auto &&...col) {
            auto addColumn = [&](auto &&c) {
              if (!first)
                ss << ", ";
              ss << tableAlias_.value_or(getAlias(get_index_of_v<Model, ModelsTuple>)) << "." << c.dbName;
              first = false;
            };
            (addColumn(col), ...);
          },
          mainModelColumns);
    } else {
      ss << columns_.at(0);
    }

    for (size_t i = 1; i < columns_.size(); i++) {
      ss << ", " << columns_.at(i);
    }

    ss << " FROM " << std::string(Model::tableName) << " AS "
       << tableAlias_.value_or(getAlias(get_index_of_v<Model, ModelsTuple>));

    if (joins_) {
      for (auto &join : *joins_) {
        ss << join.type << join.tableName << " AS " << join.tableAlias << " ON "
           << join.condition.resolvePredicates(params_);
      }
    }

    if (this->wherePredicate) {
      ss << " WHERE " << (*this->wherePredicate).resolvePredicates(params_);
    }

    if (!groupBy_.empty()) {
      ss << " GROUP BY " << groupBy_.at(0);
      for (size_t i = 1; i < groupBy_.size(); i++) {
        ss << ", " << groupBy_.at(i);
      }
    }

    if (!orderBy_.empty()) {
      ss << " ORDER BY " << orderBy_.at(0).first;
      if (orderBy_.at(0).second == Sorting::DESC)
        ss << " DESC";
      for (size_t i = 1; i < orderBy_.size(); i++) {
        ss << ", " << orderBy_.at(i).first;
        if (orderBy_.at(i).second == Sorting::DESC)
          ss << " DESC ";
      }
    }

    if (limit) {
      ss << " LIMIT " << std::to_string(limit.value());
    } else if (limit_) {
      ss << " LIMIT " << std::to_string(limit_.value());
    }

    if (offset_) {
      ss << " OFFSET " << std::to_string(offset_.value());
    }

    ss << " ; ";
    sql_ = ss.str();
  }
};

} // namespace rukh::orm

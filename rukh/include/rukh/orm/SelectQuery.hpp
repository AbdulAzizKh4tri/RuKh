#pragma once

#include <expected>
#include <optional>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>

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
#include <tuple>

namespace rukh::orm {

enum class Sorting { DESC, ASC };
enum class JoinType { INNER, LEFT, RIGHT, FULL, CROSS };
enum class SetQueryType { UNION, UNION_ALL, INTERSECT, INTERSECT_ALL, EXCEPT, EXCEPT_ALL };

template <typename... Models>
class SelectQuery : public QueryBase<SelectQuery<Models...>>, public WhereClause<SelectQuery<Models...>, Models...> {
  /*
   * Every Query has a Main Model, the first one defined in the template params.
   * This is the only one that can use methods like update, getOne, etc.
   * If you are not using JOINS or SET operations, you need not worry about this.
   */
public:
  using ModelsTuple = std::tuple<Models...>;
  using Model = std::tuple_element_t<0, ModelsTuple>;

  std::vector<SelectQuery> children;
  std::optional<SetQueryType> setQueryType = std::nullopt;

  SelectQuery() {}
  SelectQuery(const std::string &tableAlias) : tableAlias_(tableAlias) {}
  SelectQuery(const SetQueryType setType) : setQueryType(setType) {}

  SelectQuery unionQuery(const SelectQuery &otherQuery) {
    SelectQuery query(SetQueryType::UNION);
    query.children.push_back(*this);
    query.children.push_back(otherQuery);
    return query;
  }

  SelectQuery intersectQuery(const SelectQuery &otherQuery) {
    SelectQuery query(SetQueryType::INTERSECT);
    query.children.push_back(*this);
    query.children.push_back(otherQuery);
    return query;
  }

  SelectQuery exceptQuery(const SelectQuery &otherQuery) {
    SelectQuery query(SetQueryType::EXCEPT);
    query.children.push_back(*this);
    query.children.push_back(otherQuery);
    return query;
  }

  SelectQuery unionAllQuery(const SelectQuery &otherQuery) {
    SelectQuery query(SetQueryType::UNION_ALL);
    query.children.push_back(*this);
    query.children.push_back(otherQuery);
    return query;
  }

  SelectQuery intersectAllQuery(const SelectQuery &otherQuery) {
    SelectQuery query(SetQueryType::INTERSECT_ALL);
    query.children.push_back(*this);
    query.children.push_back(otherQuery);
    return query;
  }

  SelectQuery exceptAllQuery(const SelectQuery &otherQuery) {
    SelectQuery query(SetQueryType::EXCEPT_ALL);
    query.children.push_back(*this);
    query.children.push_back(otherQuery);
    return query;
  }

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
  SelectQuery &field(FieldPtr fieldPtr, const std::string &func = "", const std::string &tableAlias = "",
                     const std::string &columnAlias = "") {
    columns_.push_back(getFullColumnString(fieldPtr, func, tableAlias, columnAlias));
    return *this;
  }

  template <typename M> SelectQuery &allColumns(const std::string tableAlias = "") {
    static_assert(is_in_tuple_v<M, ModelsTuple>, "allColumns model not found in provided SelectQuery Model types");
    std::apply(
        [&](auto &&...cols) {
          (columns_.push_back(getFullColumnString(cols.fieldPtr, "", tableAlias,
                                                  tableAlias.empty() ? std::string(cols.dbName)
                                                                     : tableAlias + "_" + std::string(cols.dbName))),
           ...);
        },
        M::columns());
    return *this;
  }

  template <typename FieldPtr>
  SelectQuery &orderBy(FieldPtr fieldPtr, Sorting sorting = Sorting::ASC, const std::string &tableAlias = "") {
    orderBy_.emplace_back(getFullColumnString(fieldPtr, "", tableAlias, ""), sorting);
    return *this;
  }

  SelectQuery &distinct() {
    distinct_ = true;
    return *this;
  }

  SelectQuery &clearLimit() {
    limit_ = std::nullopt;
    return *this;
  }

  std::optional<size_t> getLimit() const { return limit_; }

  SelectQuery &limit(size_t limit) {
    limit_ = limit;
    return *this;
  }

  SelectQuery &clearOffset() {
    offset_ = std::nullopt;
    return *this;
  }

  std::optional<size_t> getOffset() const { return offset_; }

  SelectQuery &offset(size_t offset) {
    offset_ = offset_.value_or(0) + offset;
    return *this;
  }

  template <typename FieldPtr> SelectQuery &groupBy(FieldPtr fieldPtr, const std::string &tableAlias = "") {
    groupBy_.push_back(getFullColumnString(fieldPtr, "", tableAlias, ""));
    return *this;
  }

  SelectQuery &having(const Predicate<Models...> &p) {
    havingPredicate_ = p;
    return *this;
  }

  // Count the number of rows
  Task<std::expected<int64_t, db::DatabaseError>> count(db::ITransaction *transaction = nullptr) {
    buildSelectSqlAndSetParams();
    auto countSql = "SELECT COUNT(*) FROM (" + sql_ + ")";
    auto queryResult = co_await this->dispatch(transaction, countSql, this->params_);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return queryResult->rows[0].template as<int64_t>(0).value_or(0);
  }

  /*
   * Returns a single object, throws if 0 or multiple rows are returned.
   * Cannot be used with JOIN Queries that return columns from more than one table;.
   */
  Task<std::expected<Model, db::DatabaseError>> getOne(db::ITransaction *transaction = nullptr) {
    buildSelectSqlAndSetParams(0, 2);

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
    buildSelectSqlAndSetParams(0, 2);
    auto queryResult = co_await this->dispatch(transaction, sql_, params_);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    auto rowCount = queryResult->rows.size();
    if (rowCount > 1)
      throw rukh::OrmException("getOptional(): Got more than one row when expected only one");
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
    buildSelectSqlAndSetParams(0, 1);

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
    buildSelectSqlAndSetParams(0, 1);

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
  Task<std::expected<std::pair<Model, bool>, db::DatabaseError>>
  getOneOrCreate(const Model &objToCreate, db::ITransaction *transaction = nullptr) {
    if (not this->wherePredicate)
      throw rukh::OrmException("getOneOrCreate(): no where predicate set");

    auto getResult = co_await getOneOptional(transaction);
    if (not getResult)
      co_return std::unexpected(getResult.error());

    auto userOpt = *getResult;
    if (userOpt)
      co_return std::make_pair(*userOpt, false);
    auto insertResult = co_await InsertQuery<Model>().execute({objToCreate}, transaction, true);
    if (not insertResult) {
      if (insertResult.error().type == db::DbErrorType::DUPLICATE_KEY or
          insertResult.error().type == db::DbErrorType::UNIQUE_CONSTRAINT_VIOLATION) {
        auto getOneResult = co_await getOne(transaction);
        if (not getOneResult)
          co_return std::unexpected(getOneResult.error());
        co_return std::make_pair(*getOneResult, false);
      } else {
        co_return std::unexpected(insertResult.error());
      }
    }
    auto objs = (*insertResult).second;
    co_return std::make_pair(objs[0], true);
  }

  // Returns unhydrated QueryResult Object
  Task<std::expected<db::QueryResult, db::DatabaseError>> execute(db::ITransaction *transaction = nullptr) {
    buildSelectSqlAndSetParams();
    auto queryResult = co_await this->dispatch(transaction, this->sql_, this->params_);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return queryResult;
  }

  std::string getSql() {
    buildSelectSqlAndSetParams();
    return this->sql_;
  }

  std::pair<std::string, std::vector<db::DbValue>> getSqlAndParams(size_t depth = 0) {
    buildSelectSqlAndSetParams(depth);
    return {this->sql_, this->params_};
  }

  SelectQuery<Models...> &reset() {
    this->wherePredicate = std::nullopt;
    tableAlias_ = std::nullopt;
    columns_.clear();
    params_.clear();
    orderBy_.clear();
    groupBy_.clear();
    limit_ = std::nullopt;
    offset_ = std::nullopt;
    return *this;
  }

  size_t getColumnCount() const { return columns_.size(); }

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
  std::optional<Predicate<Models...>> havingPredicate_ = std::nullopt;
  bool distinct_ = false;

  static constexpr std::string_view joinTypeStrings[] = {" JOIN ", " LEFT JOIN ", " RIGHT JOIN ", " FULL JOIN ",
                                                         " CROSS JOIN "};
  static constexpr std::string_view to_string(JoinType joinType) {
    return joinTypeStrings[static_cast<std::size_t>(joinType)];
  }

  static constexpr std::string_view setQueryTypeStrings[] = {" UNION ",         " UNION ALL ", " INTERSECT ",
                                                             " INTERSECT ALL ", " EXCEPT ",    " EXCEPT ALL "};
  static constexpr std::string_view to_string(SetQueryType setType) {
    return setQueryTypeStrings[static_cast<std::size_t>(setType)];
  }

  template <typename FieldPtr>
  std::string getFullColumnString(FieldPtr fieldPtr, const std::string &func, const std::string &tableAlias,
                                  const std::string &columnAlias) {
    using FieldPtrModel = get_class_t<FieldPtr>;
    std::string asColumnAlias = columnAlias.empty() ? "" : " AS " + columnAlias;
    std::string col;
    if (tableAlias.empty()) {
      col = getAlias(get_index_of_v<FieldPtrModel, ModelsTuple>) + "." + FieldPtrModel::columnNameOf(fieldPtr);
    } else {
      col = tableAlias + "." + FieldPtrModel::columnNameOf(fieldPtr);
    }
    return (func.empty() ? func + "(" + col + ")" : col) + asColumnAlias;
  }

  // TODO: make bulidSelectSQLAndSetParams const

  void buildSelectSqlAndSetParams(const size_t depth = 0, std::optional<size_t> limit = std::nullopt) {
    params_.clear();
    std::ostringstream ss;

    if (setQueryType) {
      if (not groupBy_.empty() or havingPredicate_)
        throw rukh::OrmException("groupBy()/having() cannot be combined with union/intersect/except queries");

      auto result1 = children[0].getSqlAndParams(depth + 1);
      auto result2 = children[1].getSqlAndParams(depth + 1);
      auto sql1 = result1.first;
      auto sql2 = result2.first;

      if (children[0].setQueryType)
        sql1 = "SELECT * FROM ( " + sql1 + ") AS sub_" + std::to_string(depth) + "_0";

      if (children[1].setQueryType)
        sql2 = "SELECT * FROM ( " + sql2 + ") AS sub_" + std::to_string(depth) + "_1";

      ss << sql1 << to_string(*setQueryType) << sql2;
      params_ = result1.second;
      params_.insert(params_.end(), result2.second.begin(), result2.second.end());
    } else {
      ss << "SELECT " << (distinct_ ? " DISTINCT " : "");

      if (columns_.empty()) {
        constexpr auto mainModelColumns = Model::columns();
        bool first = true;
        std::apply(
            [&](auto &&...col) {
              auto addColumn = [&](auto &&c) {
                if (not first)
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

      if (not groupBy_.empty()) {
        ss << " GROUP BY " << groupBy_.at(0);
        for (size_t i = 1; i < groupBy_.size(); i++) {
          ss << ", " << groupBy_.at(i);
        }
        if (havingPredicate_) {
          ss << " HAVING " << havingPredicate_.value().resolvePredicates(params_);
        }
      }
    }

    // From here down applies whether this is a plain select or a compound (union/etc.) result.
    if (not orderBy_.empty()) {
      ss << " ORDER BY " << orderBy_.at(0).first;
      if (orderBy_.at(0).second == Sorting::DESC)
        ss << " DESC";
      for (size_t i = 1; i < orderBy_.size(); i++) {
        ss << ", " << orderBy_.at(i).first;
        if (orderBy_.at(i).second == Sorting::DESC)
          ss << " DESC";
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

    ss << " ";
    sql_ = ss.str();
  }
};

} // namespace rukh::orm

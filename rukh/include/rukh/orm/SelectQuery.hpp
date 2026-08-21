#pragma once

#include <expected>
#include <optional>
#include <spdlog/spdlog.h>
#include <sstream>
#include <string>
#include <tuple>

#include <rukh/Exceptions.hpp>
#include <rukh/TypeHelpers.hpp>
#include <rukh/core/Task.hpp>
#include <rukh/db/ITransaction.hpp>
#include <rukh/orm/DeleteQuery.hpp>
#include <rukh/orm/InsertQuery.hpp>
#include <rukh/orm/Predicate.hpp>
#include <rukh/orm/QueryDispatcher.hpp>
#include <rukh/orm/UpdateQuery.hpp>
#include <rukh/orm/WhereClause.hpp>
#include <rukh/orm/hydrators.hpp>

namespace rukh::orm {

template <typename... Models> class SelectQuery;

struct Cte {
  enum class Type { RECURSIVE, NONRECURSIVE };

  std::string name;
  std::string sql;
  std::vector<db::DbValue> params;
  Type type = Type::NONRECURSIVE;
};

enum class Sorting { DESC, ASC };
enum class JoinType { INNER, LEFT, RIGHT, FULL, CROSS };
enum class SetQueryType { UNION, UNION_ALL, INTERSECT, INTERSECT_ALL, EXCEPT, EXCEPT_ALL };

template <typename... Models>
class SelectQuery : public QueryDispatcher, public WhereClause<SelectQuery<Models...>, Models...> {
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
    joins_.emplace_back(std::string(JoinModel::tableName),
                        (tableAlias.value_or(getAlias(get_index_of_v<JoinModel, ModelsTuple>))), to_string(joinType),
                        predicate);
    return *this;
  }

  SelectQuery &join(const std::string &tableName, const Predicate<Models...> &predicate, const std::string tableAlias,
                    JoinType joinType = JoinType::INNER) {
    if (tableAlias.empty())
      throw OrmException("Must provide table alias for string based tableName");
    joins_.emplace_back(tableName, tableAlias, to_string(joinType), predicate);
    return *this;
  }

  SelectQuery &withCte(const std::string &name, const SelectQuery &query, Cte::Type type = Cte::Type::NONRECURSIVE) {
    bool isRecursive = type == Cte::Type::RECURSIVE;

    if (isRecursive and (not(query.setQueryType) or not(*query.setQueryType == SetQueryType::UNION or
                                                        *query.setQueryType == SetQueryType::UNION_ALL)))
      throw OrmException("Recursive CTE must have UNION or UNION ALL query");

    if (isRecursive and query.hasRecursive_)
      throw OrmException("Can only have 1 recursive CTE");

    if (hasRecursive_ and isRecursive)
      throw OrmException("Can only have 1 recursive CTE");
    hasRecursive_ = hasRecursive_ or isRecursive;

    auto [sql, params] = query.getSqlAndParams();
    ctes_.emplace_back(name, sql, params, type);
    return *this;
  }

  SelectQuery &withCte(const Cte &cte) {
    bool isRecursive = cte.type == Cte::Type::RECURSIVE;
    if (hasRecursive_ and isRecursive)
      throw OrmException("Can only have 1 recursive CTE");
    hasRecursive_ = hasRecursive_ or isRecursive;

    ctes_.push_back(cte);
    return *this;
  }

  SelectQuery &from(const std::string &tableName, const std::string &tableAlias) {
    tableName_ = tableName;
    if (tableAlias.empty())
      tableAlias_ = std::nullopt;
    else
      tableAlias_ = tableAlias;
    return *this;
  }

  template <FieldPointer FieldPtr>
  SelectQuery &field(FieldPtr fieldPtr, const std::string &tableAlias = "", const std::string &columnAlias = "") {
    columns_.push_back(getFullColumnString(fieldPtr, "", tableAlias, columnAlias));
    return *this;
  }

  template <FieldPointer FieldPtr>
  SelectQuery &functionField(const std::string &func, FieldPtr fieldPtr, const std::string &tableAlias = "",
                             const std::string &columnAlias = "") {
    columns_.push_back(getFullColumnString(fieldPtr, func, tableAlias, columnAlias));
    return *this;
  }

  SelectQuery &column(const std::string &column, const std::string &tableAlias = "",
                      const std::string &columnAlias = "") {
    columns_.push_back(getFullColumnString(column, "", tableAlias, columnAlias));
    return *this;
  }

  SelectQuery &functionColumn(const std::string &func, const std::string &column, const std::string &tableAlias = "",
                              const std::string &columnAlias = "") {
    columns_.push_back(getFullColumnString(column, func, tableAlias, columnAlias));
    return *this;
  }

  template <typename M> SelectQuery &allColumns(const std::string tableAlias = "", const std::string aliasPrefix = "") {
    static_assert(is_in_tuple_v<M, ModelsTuple>, "allColumns model not found in provided SelectQuery Model types");
    std::apply(
        [&](auto &&...cols) {
          (columns_.push_back(getFullColumnString(
               cols.fieldPtr, "", tableAlias, aliasPrefix.empty() ? "" : aliasPrefix + "_" + std::string(cols.dbName))),
           ...);
        },
        M::columns());
    return *this;
  }

  SelectQuery &orderBy(const std::string &column, Sorting sorting = Sorting::ASC, const std::string &tableAlias = "") {
    orderBy_.emplace_back(getFullColumnString(column, "", tableAlias, ""), sorting);
    return *this;
  }

  template <FieldPointer FieldPtr>
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

  template <FieldPointer FieldPtr> SelectQuery &groupBy(FieldPtr fieldPtr, const std::string &tableAlias = "") {
    groupBy_.push_back(getFullColumnString(fieldPtr, "", tableAlias, ""));
    return *this;
  }

  SelectQuery &having(const Predicate<Models...> &p) {
    havingPredicate_ = p;
    return *this;
  }

  // Count the number of rows
  core::Task<std::expected<int64_t, db::DatabaseError>> count(db::ITransaction *transaction = nullptr) {
    auto [sql, params] = buildSelectSqlAndSetParams();
    auto countSql = "SELECT COUNT(*) FROM (" + sql + ")";
    auto queryResult = co_await this->dispatch(transaction, countSql, params);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return queryResult->rows[0].template as<int64_t>(0);
  }

  /*
   * Returns a single object, throws if 0 or multiple rows are returned.
   * Cannot be used with JOIN Queries that return columns from more than one table;.
   */
  core::Task<std::expected<Model, db::DatabaseError>> getOne(db::ITransaction *transaction = nullptr) {
    auto [sql, params] = buildSelectSqlAndSetParams(0, 2);

    auto queryResult = co_await this->dispatch(transaction, sql, params);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    auto rowCount = queryResult->rows.size();

    if (rowCount > 1)
      throw rukh::OrmException("getOne(): Got more than one row, expected only one");
    if (rowCount < 1)
      throw rukh::OrmException("getOne(): Found no matching rows");

    co_return hydrateModel<Model>(queryResult->rows[0]);
  }

  /*
   * Returns a single object, throws if multiple rows are returned.
   * Cannot be used with JOIN Queries that return columns from more than one table;.
   */
  core::Task<std::expected<std::optional<Model>, db::DatabaseError>> getOneOptional(db::ITransaction *transaction) {
    auto [sql, params] = buildSelectSqlAndSetParams(0, 2);
    auto queryResult = co_await this->dispatch(transaction, sql, params);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    auto rowCount = queryResult->rows.size();
    if (rowCount > 1)
      throw rukh::OrmException("getOptional(): Got more than one row when expected only one");
    if (rowCount == 0)
      co_return std::nullopt;
    co_return hydrateModel<Model>(queryResult->rows[0]);
  };

  /*
   * Cannot be used with JOIN Queries that return columns from more than one table;.
   */
  core::Task<std::expected<std::vector<Model>, db::DatabaseError>> select(db::ITransaction *transaction = nullptr) {
    auto [sql, params] = buildSelectSqlAndSetParams();
    auto queryResult = co_await this->dispatch(transaction, sql, params);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return hydrateModel<Model>(*queryResult);
  }

  /*
   * Cannot be used with JOIN Queries that return columns from more than one table;.
   */
  core::Task<std::expected<std::optional<Model>, db::DatabaseError>> first(db::ITransaction *transaction = nullptr) {
    auto [sql, params] = buildSelectSqlAndSetParams(0, 1);

    auto queryResult = co_await this->dispatch(transaction, sql, params);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    if (queryResult->rows.empty())
      co_return std::nullopt;

    co_return hydrateModelRow<Model>(queryResult->rows[0]);
  }

  /*
   * Cannot be used with JOIN Queries that return columns from more than one table;.
   */
  core::Task<std::expected<bool, db::DatabaseError>> exists(db::ITransaction *transaction = nullptr) {
    auto [sql, params] = buildSelectSqlAndSetParams(0, 1);

    auto queryResult = co_await this->dispatch(transaction, sql, params);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return queryResult->rows.size() > 0;
  }

  /**
   * @warning
   * It is the callers responsibility to ensure that the row described by the where clause matches the values of the
   * objToCreate. Otherwise a situation may arise where the where clause returns nothing for a query, and creates a
   * completely different object based on objToCreate, which may already exist.
   *
   * @note
   * Cannot be used with JOIN Queries that return columns from more than one table;.
   */
  core::Task<std::expected<std::pair<Model, bool>, db::DatabaseError>>
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
  core::Task<std::expected<db::QueryResult, db::DatabaseError>> execute(db::ITransaction *transaction = nullptr) {
    auto [sql, params] = buildSelectSqlAndSetParams();
    auto queryResult = co_await this->dispatch(transaction, sql, params);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return queryResult;
  }

  std::string getSql() const {
    auto [sql, _] = buildSelectSqlAndSetParams();
    return sql;
  }

  std::pair<std::string, std::vector<db::DbValue>> getSqlAndParams(size_t depth = 0) const {
    return buildSelectSqlAndSetParams(depth);
  }

  size_t getColumnCount() const { return columns_.size(); }

private:
  struct Join {
    std::string tableName;
    std::string tableAlias;
    std::string_view type;
    Predicate<Models...> condition;
  };

  std::string tableName_ = std::string(Model::tableName);
  std::optional<std::string> tableAlias_ = std::nullopt;
  std::vector<Join> joins_;
  std::vector<std::string> columns_;
  std::vector<std::pair<std::string, Sorting>> orderBy_;
  std::vector<std::string> groupBy_;
  std::optional<size_t> limit_;
  std::optional<size_t> offset_;
  std::optional<Predicate<Models...>> havingPredicate_ = std::nullopt;
  bool distinct_ = false;

  std::vector<Cte> ctes_;
  bool hasRecursive_ = false;

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

  template <FieldPointer FieldPtr>
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
    return (not func.empty() ? func + "(" + col + ")" : col) + asColumnAlias;
  }

  std::string getFullColumnString(const std::string &column, const std::string &func, const std::string &tableAlias,
                                  const std::string &columnAlias) {
    std::string asColumnAlias = columnAlias.empty() ? "" : " AS " + columnAlias;
    std::string col;
    if (tableAlias.empty()) {
      col = column;
    } else {
      col = tableAlias + "." + column;
    }
    return (not func.empty() ? func + "(" + col + ")" : col) + asColumnAlias;
  }

  std::pair<std::string, std::vector<db::DbValue>>
  buildSelectSqlAndSetParams(const size_t depth = 0, std::optional<size_t> limit = std::nullopt) const {
    std::vector<db::DbValue> params;
    std::ostringstream ss;

    bool firstCte = true;
    for (const Cte &cte : ctes_) {
      ss << (firstCte ? "WITH " : " , ") << (firstCte and hasRecursive_ ? " RECURSIVE " : "") << cte.name << " AS ( "
         << cte.sql << " ) ";
      params.insert(params.end(), cte.params.begin(), cte.params.end());
      firstCte = false;
    }

    if (setQueryType) {
      if (not groupBy_.empty() or havingPredicate_)
        throw rukh::OrmException("groupBy()/having() cannot be combined with union/intersect/except queries");

      auto result1 = children[0].getSqlAndParams(depth + 1);
      auto result2 = children[1].getSqlAndParams(depth + 1);
      auto sql1 = result1.first;
      auto sql2 = result2.first;

      if (children[0].setQueryType)
        sql1 = " SELECT * FROM ( " + sql1 + ") AS sub_" + std::to_string(depth) + "_0";

      if (children[1].setQueryType)
        sql2 = " SELECT * FROM ( " + sql2 + ") AS sub_" + std::to_string(depth) + "_1";

      ss << sql1 << to_string(*setQueryType) << sql2;
      params = result1.second;
      params.insert(params.end(), result2.second.begin(), result2.second.end());
    } else {
      ss << " SELECT " << (distinct_ ? " DISTINCT " : "");

      if (columns_.empty()) {
        if (tableName_ != Model::tableName) {
          throw OrmException("Must provide SELECT column list when overriding tableName");
        }

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

      ss << " FROM " << tableName_ << " AS " << tableAlias_.value_or(getAlias(get_index_of_v<Model, ModelsTuple>));

      if (not joins_.empty()) {
        for (auto &join : joins_) {
          ss << join.type << join.tableName << " AS " << join.tableAlias << " ON "
             << join.condition.resolvePredicates(params);
        }
      }

      if (this->wherePredicate) {
        ss << " WHERE " << (*this->wherePredicate).resolvePredicates(params);
      }

      if (not groupBy_.empty()) {
        ss << " GROUP BY " << groupBy_.at(0);
        for (size_t i = 1; i < groupBy_.size(); i++) {
          ss << ", " << groupBy_.at(i);
        }
        if (havingPredicate_) {
          ss << " HAVING " << havingPredicate_.value().resolvePredicates(params);
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
    return {ss.str(), params};
  }
};

} // namespace rukh::orm

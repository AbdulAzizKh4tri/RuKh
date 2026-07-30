#pragma once

#include <optional>
#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/Task.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>
#include <rukh/orm/DeleteQuery.hpp>
#include <rukh/orm/Predicate.hpp>
#include <rukh/orm/QueryBase.hpp>
#include <rukh/orm/UpdateQuery.hpp>
#include <rukh/orm/WhereClause.hpp>
#include <rukh/orm/hydrators.hpp>

namespace rukh::orm {

template <typename Model>
class SelectQuery : public WhereClause<Model, SelectQuery<Model>>, public QueryBase<Model, SelectQuery<Model>> {
public:
  SelectQuery &field(const std::string &column) {
    if (not Model::isValidColumnName(column))
      throw rukh::OrmException("Failed to add column to query: unknown column '" + column + "' on " + this->tblName);
    changed = true;
    columns_.push_back(column);
    return *this;
  }

  template <typename FieldT> SelectQuery &field(FieldT Model::*fieldPtr) {
    return field(Model::columnNameOf(fieldPtr));
  }

  SelectQuery &orderBy(const std::string &order, bool desc = false) {
    if (not Model::isValidColumnName(order))
      throw rukh::OrmException("orderBy: unknown column '" + order + "' on " + this->tblName);
    changed = true;
    orderBy_.emplace_back(order, desc);
    return *this;
  }

  template <typename FieldT> SelectQuery &orderBy(FieldT Model::*fieldPtr, bool desc = false) {
    return orderBy(Model::columnNameOf(fieldPtr), desc);
  }

  SelectQuery &limit(size_t limit) {
    if (limit == 0)
      throw rukh::OrmException("Limit must be greater than 0");
    changed = true;
    limit_ = limit;
    return *this;
  }

  SelectQuery &offset(size_t offset) {
    changed = true;
    offset_ = offset_.value_or(0) + offset;
    return *this;
  }

  SelectQuery &groupBy(const std::string &column) {
    if (not Model::isValidColumnName(column))
      throw rukh::OrmException("Failed to add column to groupBy: unknown column '" + column + "' on " + this->tblName);

    changed = true;
    groupBy_.push_back(column);
    return *this;
  }

  template <typename FieldT> SelectQuery &groupBy(FieldT Model::*fieldPtr) {
    return groupBy(Model::columnNameOf(fieldPtr));
  }

  Task<std::expected<int64_t, db::DatabaseError>> count(db::ITransaction *transaction = nullptr,
                                                        bool distinct = false) {
    std::string countCol = distinct ? "DISTINCT COUNT(*)" : "COUNT(*)";
    std::string countSql = "SELECT " + countCol + " FROM " + this->tblName + " ";
    std::vector<db::DbValue> countParams;

    if (this->wherePredicate) {
      countSql += " WHERE ";
      countSql += Predicate<Model>::resolvePredicates(*this->wherePredicate, countParams);
    }

    auto queryResult = co_await this->dispatch(transaction, countSql, countParams);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return queryResult->rows[0].template as<int64_t>(0).value_or(0);
  }

  template <typename FieldT>
  Task<std::expected<int64_t, db::DatabaseError>> count(const std::string &col, db::ITransaction *transaction = nullptr,
                                                        bool distinct = false) {
    if (not Model::validColumnNames().contains(col))
      co_return std::unexpected(
          db::DatabaseError{db::DbErrorType::INVALID_COLUMN, "Unknown column '" + col + "' on " + this->tblName});

    std::string countCol = distinct ? "DISTINCT COUNT(" + col + ")" : "COUNT(" + col + ")";
    std::string countSql = "SELECT " + countCol + " FROM " + this->tblName + " ";
    std::vector<db::DbValue> countParams;

    if (this->wherePredicate) {
      countSql += " WHERE ";
      countSql += Predicate<Model>::resolvePredicates(*this->wherePredicate, countParams);
    }

    auto queryResult = co_await this->dispatch(transaction, countSql, countParams);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return queryResult->rows[0].template as<int64_t>(0).value_or(0);
  }

  template <typename FieldT>
  Task<std::expected<int64_t, db::DatabaseError>>
  count(FieldT Model::*fieldPtr, db::ITransaction *transaction = nullptr, bool distinct = false) {
    co_return co_await count(Model::columnNameOf(fieldPtr), transaction, distinct);
  }

  Task<std::expected<std::vector<Model>, db::DatabaseError>> get(db::ITransaction *transaction = nullptr) {
    buildSelectSqlAndSetParams();
    auto queryResult = co_await this->dispatch(transaction, this->sql_, this->params_);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return hydrate<Model>(*queryResult);
  }

  Task<std::expected<std::optional<Model>, db::DatabaseError>> first(db::ITransaction *transaction = nullptr) {
    std::optional<size_t> oldLimit = limit_;
    limit_ = 1;
    changed = true;
    buildSelectSqlAndSetParams();
    limit_ = oldLimit;
    changed = true;

    auto queryResult = co_await this->dispatch(transaction, this->sql_, this->params_);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    if (queryResult->rows.empty())
      co_return std::nullopt;

    co_return hydrate<Model>(queryResult->rows[0]);
  }

  Task<std::expected<bool, db::DatabaseError>> exists(db::ITransaction *transaction = nullptr) {
    buildSelectSqlAndSetParams();

    auto queryResult = co_await this->dispatch(transaction, this->sql_, this->params_);
    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return queryResult->rows.size() > 0;
  }

  Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  update(const Model &obj, db::ITransaction *transaction = nullptr, bool returning = false) {
    UpdateQuery<Model> updateQuery;
    for (auto &col : columns_) {
      if (not Model::isValidColumnName(col))
        throw rukh::OrmException("Failed to add column to query: unknown column '" + col + "' on " + this->tblName);
      updateQuery.field(col);
    }
    co_return co_await updateQuery.where(this->wherePredicate.value_or(Predicate<Model>::truePredicate()))
        .execute(obj, transaction, returning);
  }

  Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  destroy(db::ITransaction *transaction = nullptr, bool returning = false) {
    co_return co_await DeleteQuery<Model>()
        .where(this->wherePredicate.value_or(Predicate<Model>::truePredicate()))
        .execute(transaction, returning);
  }

  SelectQuery<Model> &reset() {
    changed = true;
    this->whereChanged = true;
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
  std::string sql_;
  bool changed = true;
  std::vector<std::string> columns_;
  std::vector<db::DbValue> params_;
  std::vector<std::pair<std::string, bool>> orderBy_;
  std::vector<std::string> groupBy_;
  std::optional<size_t> limit_;
  std::optional<size_t> offset_;

  void buildSelectSqlAndSetParams() {
    if (not changed and not this->whereChanged)
      return;

    changed = false;
    this->whereChanged = false;

    this->sql_ = "SELECT ";
    params_.clear();

    if (columns_.empty()) {
      this->sql_ += "*";
    } else {
      this->sql_ += columns_.at(0);
    }

    for (size_t i = 1; i < columns_.size(); i++) {
      this->sql_ += ", ";
      this->sql_ += columns_.at(i);
    }

    this->sql_ += " FROM " + this->tblName + " ";

    if (this->wherePredicate) {
      this->sql_ += " WHERE ";
      this->sql_ += Predicate<Model>::resolvePredicates(*this->wherePredicate, this->params_);
    }

    if (!groupBy_.empty()) {
      this->sql_ += " GROUP BY ";
      this->sql_ += groupBy_.at(0);
      for (size_t i = 1; i < groupBy_.size(); i++) {
        this->sql_ += ", ";
        this->sql_ += groupBy_.at(i);
      }
    }

    if (!orderBy_.empty()) {
      this->sql_ += " ORDER BY ";
      this->sql_ += orderBy_.at(0).first;
      if (orderBy_.at(0).second)
        this->sql_ += " DESC";
      for (size_t i = 1; i < orderBy_.size(); i++) {
        this->sql_ += ", ";
        this->sql_ += orderBy_.at(i).first;
        if (orderBy_.at(i).second)
          this->sql_ += " DESC";
      }
    }

    if (limit_) {
      this->sql_ += " LIMIT " + std::to_string(limit_.value());
    }

    if (offset_) {
      this->sql_ += " OFFSET " + std::to_string(offset_.value());
    }
  }
};

} // namespace rukh::orm

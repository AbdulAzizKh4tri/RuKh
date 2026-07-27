#pragma once

#include "rukh/orm/UpdateQuery.hpp"
#include <optional>
#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/Task.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/orm/DeleteQuery.hpp>
#include <rukh/orm/Predicate.hpp>
#include <rukh/orm/WhereClause.hpp>
#include <rukh/orm/hydrators.hpp>

namespace rukh::orm {

template <typename Model> class SelectQuery : public WhereClause<Model, SelectQuery<Model>> {
public:
  SelectQuery &column(const std::string &column) {
    if (not Model::isValidColumnName(column))
      throw rukh::OrmException("Failed to add column to query: unknown column '" + column + "' on " + Model::tableName);
    changed = true;
    columns_.push_back(column);
    return *this;
  }

  template <typename FieldT> SelectQuery &column(FieldT Model::*fieldPtr) {
    return column(Model::columnNameOf(fieldPtr));
  }

  SelectQuery &orderBy(const std::string &order, bool desc = false) {
    if (not Model::isValidColumnName(order))
      throw rukh::OrmException("orderBy: unknown column '" + order + "' on " + Model::tableName);
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
      throw rukh::OrmException("Failed to add column to groupBy: unknown column '" + column + "' on " +
                               Model::tableName);

    changed = true;
    groupBy_.push_back(column);
    return *this;
  }

  template <typename FieldT> SelectQuery &groupBy(FieldT Model::*fieldPtr) {
    return groupBy(Model::columnNameOf(fieldPtr));
  }

  Task<int64_t> count(bool distinct = false) {
    std::string countCol = distinct ? "DISTINCT COUNT(*)" : "COUNT(*)";
    std::string countSql = "SELECT " + countCol + " FROM " + Model::tableName + " ";
    std::vector<db::DbValue> countParams;

    if (this->wherePredicate) {
      countSql += " WHERE ";
      countSql += Predicate<Model>::resolvePredicates(*this->wherePredicate, countParams);
    }

    auto queryResult = co_await Model::threadPool->submit(
        [this, countSql, countParams]() -> std::expected<db::QueryResult, db::DatabaseError> {
          return db_->executeQuery(countSql, countParams);
        });

    if (not queryResult) {
      SPDLOG_ERROR("Error executing query: {}", countSql);
      SPDLOG_ERROR("DatabaseError: {}", queryResult.error().message);
      co_return -1;
    }

    co_return queryResult->rows[0].template as<int64_t>(0).value_or(0);
  }

  template <typename FieldT> Task<int64_t> count(const std::string &col, bool distinct = false) {
    if (not Model::validColumnNames().contains(col))
      throw rukh::OrmException("Failed to add column to groupBy: unknown column '" + col + "' on " + Model::tableName);

    std::string countCol = distinct ? "DISTINCT COUNT(" + col + ")" : "COUNT(" + col + ")";
    std::string countSql = "SELECT " + countCol + " FROM " + Model::tableName + " ";
    std::vector<db::DbValue> countParams;

    if (this->wherePredicate) {
      countSql += " WHERE ";
      countSql += Predicate<Model>::resolvePredicates(*this->wherePredicate, countParams);
    }

    auto queryResult = co_await Model::threadPool->submit(
        [this, countSql, countParams]() -> std::expected<db::QueryResult, db::DatabaseError> {
          return db_->executeQuery(countSql, countParams);
        });

    if (not queryResult) {
      SPDLOG_ERROR("Error executing query: {}", countSql);
      SPDLOG_ERROR("DatabaseError: {}", queryResult.error().message);
      co_return -1;
    }

    co_return queryResult->rows[0].template as<int64_t>(0).value_or(0);
  }

  template <typename FieldT> Task<int64_t> count(FieldT Model::*fieldPtr, bool distinct = false) {
    co_return count(Model::columnNameOf(fieldPtr), distinct);
  }

  Task<std::optional<std::vector<Model>>> get() {
    buildSelectSqlAndSetParams();
    auto queryResult = co_await Model::threadPool->submit(
        [this]() -> std::expected<db::QueryResult, db::DatabaseError> { return db_->executeQuery(sql_, params_); });

    if (not queryResult) {
      SPDLOG_ERROR("Error executing query: {}", sql_);
      SPDLOG_ERROR("DatabaseError: {}", queryResult.error().message);
      co_return std::nullopt;
    }

    co_return hydrate<Model>(*queryResult);
  }

  Task<std::optional<Model>> first() {
    changed = true;
    std::optional<size_t> oldLimit = limit_;
    limit_ = 1;
    buildSelectSqlAndSetParams();
    limit_ = oldLimit;

    auto queryResult = co_await Model::threadPool->submit(
        [this]() -> std::expected<db::QueryResult, db::DatabaseError> { return db_->executeQuery(sql_, params_); });

    if (not queryResult) {
      SPDLOG_ERROR("Error executing query: {}", sql_);
      SPDLOG_ERROR("DatabaseError: {}", queryResult.error().message);
      co_return std::nullopt;
    }

    if (queryResult->rows.empty())
      co_return std::nullopt;

    co_return hydrate<Model>(queryResult->rows[0]);
  }

  Task<bool> exists() {
    buildSelectSqlAndSetParams();

    auto queryResult =
        co_await Model::threadPool->submit([this]() -> std::expected<db::QueryResult, db::DatabaseError> {
          return db_->executeQuery("SELECT EXISTS (" + sql_ + ")", params_);
        });

    if (not queryResult) {
      SPDLOG_ERROR("Error executing query: {}", sql_);
      SPDLOG_ERROR("DatabaseError: {}", queryResult.error().message);
      co_return false;
    }

    co_return queryResult->rows.size() > 0;
  }

  Task<std::pair<size_t, std::vector<Model>>> update(bool returning) {
    UpdateQuery<Model> updateQuery;
    for (auto &col : columns_) {
      updateQuery.column(col);
    }
    co_return co_await updateQuery.where(this->wherePredicate).execute(returning);
  }

  Task<std::pair<size_t, std::vector<Model>>> destroy(bool returning) {
    co_return co_await DeleteQuery<Model>().where(this->wherePredicate).execute(returning);
  }

  std::vector<rukh::db::DbValue> getParams() const { return params_; }
  std::string getSql() const { return sql_; }

  SelectQuery<Model> &reset() {
    changed = true;
    this->whereChanged = true;
    this->wherePredicate = std::nullopt;
    columns_.clear();
    params_.clear();
    orderBy_.clear();
    groupBy_.clear();
    limit_ = std::nullopt;
    offset_ = std::nullopt;
    return *this;
  }

private:
  db::IDatabase *db_ = Model::db;
  std::string sql_;
  bool changed = true;
  std::vector<std::string> columns_;
  std::vector<rukh::db::DbValue> params_;
  std::vector<std::pair<std::string, bool>> orderBy_;
  std::vector<std::string> groupBy_;
  std::optional<size_t> limit_;
  std::optional<size_t> offset_;

  void buildSelectSqlAndSetParams() {
    if (not changed and not this->whereChanged)
      return;

    changed = false;
    this->whereChanged = false;

    sql_ = "SELECT ";
    if (columns_.empty()) {
      sql_ += "*";
    } else {
      sql_ += columns_.at(0);
    }

    for (size_t i = 1; i < columns_.size(); i++) {
      sql_ += ", ";
      sql_ += columns_.at(i);
    }

    sql_ += " FROM " + Model::tableName + " ";

    if (this->wherePredicate) {
      sql_ += " WHERE ";
      sql_ += Predicate<Model>::resolvePredicates(*this->wherePredicate, params_);
    }

    if (!groupBy_.empty()) {
      sql_ += " GROUP BY ";
      sql_ += groupBy_.at(0);
      for (size_t i = 1; i < groupBy_.size(); i++) {
        sql_ += ", ";
        sql_ += groupBy_.at(i);
      }
    }

    if (!orderBy_.empty()) {
      sql_ += " ORDER BY ";
      sql_ += orderBy_.at(0).first;
      if (orderBy_.at(0).second)
        sql_ += " DESC";
      for (size_t i = 1; i < orderBy_.size(); i++) {
        sql_ += ", ";
        sql_ += orderBy_.at(i).first;
        if (orderBy_.at(i).second)
          sql_ += " DESC";
      }
    }

    if (limit_) {
      sql_ += " LIMIT " + std::to_string(limit_.value());
    }

    if (offset_) {
      sql_ += " OFFSET " + std::to_string(offset_.value());
    }

    SPDLOG_DEBUG("SELECT Query: {}", sql_);
  }
};

} // namespace rukh::orm

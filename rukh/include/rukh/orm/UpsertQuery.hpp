#pragma once

#include <cstddef>
#include <expected>
#include <sstream>

#include <rukh/Exceptions.hpp>
#include <rukh/core/Task.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/db/ITransaction.hpp>
#include <rukh/orm/Column.hpp>
#include <rukh/orm/Predicate.hpp>
#include <rukh/orm/QueryDispatcher.hpp>
#include <rukh/orm/hydrators.hpp>

namespace rukh::orm {

// TODO: add conflict field validation. Decide what an empty conflictColumns_ means.
//
// TODO: Figure out a way to know whether it's an update or an insert (Without schema changes, or getting DB specific.).
template <typename Model> class UpsertQuery : public QueryDispatcher {
public:
  core::Task<std::expected<std::pair<size_t, std::vector<Model>>, db::DatabaseError>>
  execute(const std::vector<Model> &objs, db::ITransaction *transaction = nullptr, bool returning = false) {
    if (objs.empty())
      co_return std::make_pair(0, std::vector<Model>());

    buildUpsertSqlAndSetParams(objs, returning);

    auto queryResult = co_await this->dispatch(transaction, sql_, params_);

    if (not queryResult)
      co_return std::unexpected(queryResult.error());

    co_return std::make_pair(queryResult->affectedRows, hydrateModel<Model>(*queryResult));
  }

  template <typename FieldPtr> UpsertQuery &conflictFields(FieldPtr fieldPtr) {
    conflictColumns_.push_back(Model::columnNameOf(fieldPtr));
    return *this;
  }

  UpsertQuery &onConflictDoNothing() {
    doNothing_ = true;
    return *this;
  }

  template <typename FieldPtr> UpsertQuery &updateFields(FieldPtr fieldPtr) {
    doNothing_ = false;
    columnsToUpdate_.push_back(Model::columnNameOf(fieldPtr));
    return *this;
  }

private:
  std::string sql_;
  bool doNothing_ = false;
  std::vector<std::string> columnsToUpdate_;
  std::vector<std::string> conflictColumns_;
  std::vector<rukh::db::DbValue> params_;

  void buildUpsertSqlAndSetParams(const std::vector<Model> &objs, bool returning = false) {
    params_.clear();
    std::ostringstream ss;
    ss << sqlInit << " VALUES " << valuesListStringAndParams(objs);

    if (doNothing_)
      ss << " ON CONFLICT " << conflictColumnList() << " DO NOTHING";
    else
      ss << " ON CONFLICT " << conflictColumnList() << " DO UPDATE SET " << modelUpdateValueList();

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

  std::string conflictColumnList() {
    std::ostringstream oss;
    bool first = true;

    oss << " ( ";
    std::apply(
        [&](auto &&...col) {
          auto handle = [&](auto &&c) {
            bool notSelected =
                not conflictColumns_.empty() and
                std::find(conflictColumns_.begin(), conflictColumns_.end(), c.dbName) == conflictColumns_.end();
            if (notSelected)
              return;

            if (not first)
              oss << ", ";
            oss << c.dbName << " ";
            first = false;
          };
          (handle(col), ...);
        },
        Model::columns());
    oss << " ) ";
    return oss.str();
  }

  std::string modelUpdateValueList() {
    std::ostringstream oss;
    bool first = true;
    std::apply(
        [&](auto &&...col) {
          auto handle = [&](auto &&c) {
            bool notSelected =
                not columnsToUpdate_.empty() and
                std::find(columnsToUpdate_.begin(), columnsToUpdate_.end(), c.dbName) == columnsToUpdate_.end();
            if (c.isPrimaryKey or notSelected or columnShouldBeSkipped(c.autoUpdateMode))
              return;

            if (not first)
              oss << ", ";
            oss << c.dbName << " = excluded." << c.dbName;
            first = false;
          };
          (handle(col), ...);
        },
        Model::columns());
    oss << " ";
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

  static inline bool columnShouldBeSkipped(AutoGenerate policy) {
    return policy == AutoGenerate::DB_INCREMENT or policy == AutoGenerate::DEFAULT or policy == AutoGenerate::DB_NOW;
  }

  static inline bool columnShouldBeSkipped(AutoUpdate policy) {
    return policy == AutoUpdate::DB_NOW or policy == AutoUpdate::LOCKED;
  }

  inline static std::string sqlInit =
      "INSERT INTO " + std::string(Model::tableName) + " (" + modelColumnListString() + ") ";
};

} // namespace rukh::orm

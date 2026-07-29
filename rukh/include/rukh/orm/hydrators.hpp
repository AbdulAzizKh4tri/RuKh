#pragma once

#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/concepts.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/orm/Column.hpp>

namespace rukh::orm {

template <typename Model, typename FieldT>
bool hydrateOne(Model &obj, const Column<Model, FieldT> &col, const db::Row &row) {
  if constexpr (OptionalT<FieldT>) {
    if (row.isNull(col.dbName)) {
      obj.*(col.fieldPtr) = std::nullopt;
      return true;
    }
    auto val = row.as<typename FieldT::value_type>(col.dbName);
    if (not val) {
      SPDLOG_ERROR("Failed to hydrate column: {}", col.dbName);
      return false;
    }
    obj.*(col.fieldPtr) = *val;
    return true;
  } else {
    auto val = row.as<FieldT>(col.dbName);
    if (not val) {
      return false;
    }
    obj.*(col.fieldPtr) = std::move(*val);
    return true;
  }
}

template <typename Model> Model hydrate(const rukh::db::Row &row) {
  Model obj;
  bool ok = true;

  std::apply(
      [&](auto &&...col) {
        auto handle = [&](auto &&c) {
          if (not ok)
            return;
          ok = hydrateOne(obj, c, row);
        };
        (handle(col), ...);
      },
      Model::columns());

  if (not ok) {
    throw rukh::OrmException("Failed to hydrate row: " + obj.tableName + ": " + row.toString());
  }

  obj.setPersisted();
  return obj;
}

template <typename Model> std::vector<Model> hydrate(rukh::db::QueryResult &queryResult) {
  std::vector<Model> result;
  for (auto &row : queryResult.rows) {
    result.push_back(hydrate<Model>(row));
  }
  return result;
}

template <typename Model> std::vector<Model> hydrate(std::vector<rukh::db::Row> &rows) {
  std::vector<Model> result;
  for (auto &row : rows) {
    result.push_back(hydrate<Model>(row));
  }
  return result;
}

} // namespace rukh::orm

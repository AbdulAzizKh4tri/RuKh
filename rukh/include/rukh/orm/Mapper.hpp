#pragma once

#include <rukh/Exceptions.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/orm/Column.hpp>

template <typename Model, typename FieldT>
bool hydrateOne(Model &obj, const rukh::orm::Column<FieldT, Model> &col, const rukh::db::Row &row) {
  auto val = row.as<FieldT>(col.name); // std::optional<FieldT>
  if (not val)
    return false; // couldn't find column or wrong type
  obj.*(col.ptr) = std::move(*val);
  return true;
}

template <typename Model> Model hydrate(const rukh::db::Row &row) {
  Model obj;
  bool ok = true;

  std::apply([&](auto &&...col) { ((ok = ok && hydrateOne(obj, col, row)), ...); }, Model::columns());

  if (not ok) {
    auto rowId = *(row.as<int64_t>("id"));
    throw rukh::OrmException("Failed to hydrate row: " + obj.modelName + " " + std::to_string(rowId));
  }

  return obj;
}

template <typename Model> std::vector<Model> hydrate(rukh::db::QueryResult &queryResult) {
  std::vector<Model> result;
  for (auto &row : queryResult.rows) {
    result.push_back(hydrate<Model>(row));
  }
  return result;
}

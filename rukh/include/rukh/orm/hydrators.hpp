#pragma once

#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/TypeHelpers.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/orm/Column.hpp>

namespace rukh::orm {

template <typename Model, typename FieldT>
bool hydrateColumn(Model &obj, const Column<Model, FieldT> &col, const db::Row &row, const std::string &prefix = "") {
  std::string lookupName = prefix.empty() ? std::string(col.dbName) : (prefix + "_" + std::string(col.dbName));
  if constexpr (OptionalT<FieldT>) {
    if (row.isNull(lookupName)) {
      obj.*(col.fieldPtr) = std::nullopt;
      return true;
    }
    auto val = row.as<typename FieldT::value_type>(lookupName);
    if (not val) {
      SPDLOG_ERROR("Failed to hydrate column: {}", lookupName);
      return false;
    }
    obj.*(col.fieldPtr) = std::move(*val);
    return true;
  } else {
    auto val = row.as<FieldT>(lookupName);
    if (not val) {
      SPDLOG_ERROR("Failed to hydrate column: {}", lookupName);
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
          ok = hydrateColumn(obj, c, row);
        };
        (handle(col), ...);
      },
      Model::columns());

  if (not ok) {
    throw rukh::OrmException("Failed to hydrate row: " + std::string(obj.tableName) + ": " + row.toString());
  }

  obj.setPersisted();
  return obj;
}

template <typename Model> std::vector<Model> hydrate(const rukh::db::QueryResult &queryResult) {
  std::vector<Model> result;
  for (auto &row : queryResult.rows) {
    result.push_back(hydrate<Model>(row));
  }
  return result;
}

template <typename Model> std::vector<Model> hydrate(const std::vector<rukh::db::Row> &rows) {
  std::vector<Model> result;
  for (auto &row : rows) {
    result.push_back(hydrate<Model>(row));
  }
  return result;
}

template <size_t I, typename ModelsTuple, typename AliasTuple>
void hydrateModelAt(const rukh::db::Row &row, ModelsTuple &objTuple, const AliasTuple &aliases) {
  auto &obj = std::get<I>(objTuple);
  const auto alias = std::get<I>(aliases);

  bool ok = true;
  std::apply(
      [&](auto &&...col) {
        auto handle = [&](auto &&c) {
          if (not ok)
            return;
          ok = hydrateColumn(obj, c, row, alias);
        };
        (handle(col), ...);
      },
      obj.columns());

  if (not ok) {
    throw rukh::OrmException("Failed to hydrate row: " + std::string(obj.tableName) + ": " + row.toString());
  }
}

template <typename ModelsTuple, typename... Aliases>
ModelsTuple hydrateJoinedRow(const rukh::db::Row &row, Aliases... aliases) {

  ModelsTuple objTuple;
  const auto aliasTuple = std::make_tuple(aliases...);

  [&]<size_t... I>(std::index_sequence<I...>) {
    (hydrateModelAt<I>(row, objTuple, aliasTuple), ...);
  }(std::make_index_sequence<std::tuple_size_v<ModelsTuple>>{});

  std::apply([&](auto &&...obj) { (obj.setPersisted(), ...); }, objTuple);

  return objTuple;
}

template <typename ModelsTuple, typename... Aliases>
auto hydrateJoined(const rukh::db::QueryResult &queryResult, Aliases... aliases) {
  std::vector<ModelsTuple> result;
  for (const auto &row : queryResult.rows) {
    result.push_back(hydrateJoinedRow<ModelsTuple>(row, aliases...));
  }
  return result;
}

} // namespace rukh::orm

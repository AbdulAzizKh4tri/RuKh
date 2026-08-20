#pragma once

#include <spdlog/spdlog.h>

#include <rukh/Exceptions.hpp>
#include <rukh/TypeHelpers.hpp>
#include <rukh/db/DbTypes.hpp>
#include <rukh/db/IDatabase.hpp>
#include <rukh/orm/Column.hpp>

namespace rukh::orm {

template <typename Model, typename FieldT>
void hydrateModelColumn(Model &obj, const Column<Model, FieldT> &col, const db::Row &row,
                        const std::string &prefix = "") {
  std::string lookupName = prefix.empty() ? std::string(col.dbName) : (prefix + "_" + std::string(col.dbName));
  if constexpr (OptionalT<FieldT>) {
    if (row.isNull(lookupName)) {
      obj.*(col.fieldPtr) = std::nullopt;
      return;
    }

    auto val = row.as<typename FieldT::value_type>(lookupName);
    obj.*(col.fieldPtr) = std::move(val);
  } else {
    auto val = row.as<FieldT>(lookupName);
    obj.*(col.fieldPtr) = std::move(val);
  }
}

template <typename Model> Model hydrateModelRow(const rukh::db::Row &row) {
  Model obj;

  std::apply(
      [&](auto &&...col) {
        auto handle = [&](auto &&c) { hydrateModelColumn(obj, c, row); };
        (handle(col), ...);
      },
      Model::columns());

  obj.setPersisted();
  return obj;
}

template <typename Model> std::vector<Model> hydrateModel(const rukh::db::QueryResult &queryResult) {
  std::vector<Model> result;
  for (auto &row : queryResult.rows) {
    result.push_back(hydrateModelRow<Model>(row));
  }
  return result;
}

template <size_t I, typename ModelsTuple, typename AliasTuple>
void hydrateModelAt(const rukh::db::Row &row, ModelsTuple &objTuple, const AliasTuple &aliases) {
  auto &obj = std::get<I>(objTuple);
  const auto alias = std::get<I>(aliases);

  std::apply(
      [&](auto &&...col) {
        auto handle = [&](auto &&c) { hydrateModelColumn(obj, c, row, alias); };
        (handle(col), ...);
      },
      obj.columns());
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
std::vector<ModelsTuple> hydrateJoined(const rukh::db::QueryResult &queryResult, Aliases... aliases) {
  std::vector<ModelsTuple> result;
  for (const auto &row : queryResult.rows) {
    result.push_back(hydrateJoinedRow<ModelsTuple>(row, aliases...));
  }
  return result;
}

template <size_t I, Tuple TypesTuple, Tuple ColumnNamesTuple>
void hydrateTupleValueAt(const db::Row &row, TypesTuple &valuesTuple, ColumnNamesTuple ColumnNames) {

  using ValueType = std::tuple_element_t<I, TypesTuple>;
  ValueType &result = std::get<I>(valuesTuple);
  const auto columnName = std::get<I>(ColumnNames);

  if constexpr (OptionalT<ValueType>) {
    if (row.isNull(columnName)) {
      result = std::nullopt;
      return;
    }

    auto val = row.as<typename ValueType::value_type>(columnName);
    result = std::move(val);
  } else {
    auto val = row.as<ValueType>(columnName);
    result = std::move(val);
  }
}

template <Tuple TypesTuple, Tuple ColumnNamesTuple>
TypesTuple hydrateTupleRow(const rukh::db::Row &row, ColumnNamesTuple columnNames) {
  TypesTuple valuesTuple;

  [&]<size_t... I>(std::index_sequence<I...>) {
    (hydrateTupleValueAt<I>(row, valuesTuple, columnNames), ...);
  }(std::make_index_sequence<std::tuple_size_v<TypesTuple>>{});

  return valuesTuple;
}

template <Tuple TypesTuple, Tuple ColumnNamesTuple>
std::vector<TypesTuple> hydrateTuple(const rukh::db::QueryResult &queryResult, ColumnNamesTuple columnNames) {
  std::vector<TypesTuple> result;
  for (const auto &row : queryResult.rows) {
    result.push_back(hydrateTupleRow<TypesTuple>(row, columnNames));
  }
  return result;
}

} // namespace rukh::orm

#pragma once

#include <string_view>

#include <rukh/TypeHelpers.hpp>
#include <rukh/db/DbTypes.hpp>

namespace rukh::orm {

enum class AutoGenerate { OFF, DB_INCREMENT, DEFAULT, DB_NOW, CUSTOM };
//TODO: Rename to UpdateMode maybe
enum class AutoUpdate { OFF, DB_NOW, CUSTOM, LOCKED };
enum class JsonSerializationMode { OFF, AUTO, CUSTOM };

template <typename Model, typename FieldT> struct Column {
  FieldT Model::*const fieldPtr;
  const std::string_view dbName;
  const bool isPrimaryKey = false;
  const bool index = false;

  const AutoGenerate autoGenerateMode = AutoGenerate::OFF;
  const db::DbValue defaultValue = nullptr;
  db::DbValue (Model::*const customGenerator)() const = nullptr;

  const AutoUpdate autoUpdateMode = AutoUpdate::OFF;
  db::DbValue (Model::*const customUpdator)() const = nullptr;

  const JsonSerializationMode jsonSerializationMode = JsonSerializationMode::AUTO;
  std::optional<nlohmann::json> (*const jsonSerializer)(const FieldT &) = nullptr;
};

} // namespace rukh::orm
